/**
 * @file missiles.cpp
 *
 * Implementation of missile functionality.
 */
#include "missiles.h"

#include <climits>
#include <cstdint>

#include "control.h"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "dead.h"
#ifdef _DEBUG
#include "debug.h"
#endif
#include "engine/backbuffer_state.hpp"
#include "engine/load_file.hpp"
#include "engine/points_in_rectangle_range.hpp"
#include "engine/random.hpp"
#include "init.h"
#include "inv.h"
#include "levels/trigs.h"
#include "lighting.h"
#include "monster.h"
#include "spells.h"
#include "utils/str_cat.hpp"

namespace devilution {

std::list<Missile> Missiles;
bool MissilePreFlag;

namespace {

int AddClassHealingBonus(int hp, HeroClass heroClass)
{
	switch (heroClass) {
	case HeroClass::Warrior:
	case HeroClass::Monk:
	case HeroClass::Barbarian:
		return hp * 2;
	case HeroClass::Rogue:
	case HeroClass::Bard:
		return hp + hp / 2;
	default:
		return hp;
	}
}

int ScaleSpellEffect(int base, int spellLevel)
{
	for (int i = 0; i < spellLevel; i++) {
		base += base / 8;
	}

	return base;
}

int GenerateRndSum(int range, int iterations)
{
	int value = 0;
	for (int i = 0; i < iterations; i++) {
		value += GenerateRnd(range);
	}

	return value;
}

bool CheckBlock(Point from, Point to)
{
	while (from != to) {
		from += GetDirection(from, to);
		if (TileHasAny(dPiece[from.x][from.y], TileProperties::Solid))
			return true;
	}

	return false;
}

Monster *FindClosest(Point source, int rad)
{
	std::optional<Point> monsterPosition = FindClosestValidPosition(
	    [&source](Point target) {
		    // search for a monster with clear line of sight
		    return InDungeonBounds(target) && dMonster[target.x][target.y] > 0 && !CheckBlock(source, target);
	    },
	    source, 1, rad);

	if (monsterPosition) {
		int mid = dMonster[monsterPosition->x][monsterPosition->y];
		return &Monsters[mid - 1];
	}

	return nullptr;
}

Player *FindClosestPlayer(Point source, int rad)
{
	std::optional<Point> playerPosition = FindClosestValidPosition(
	    [&source](Point target) {
		    // search for a player with clear line of sight
		    return InDungeonBounds(target) && dPlayer[target.x][target.y] != 0 && !CheckBlock(source, target);
	    },
	    source, 1, rad);

	if (playerPosition) {
		int pid = dPlayer[playerPosition->x][playerPosition->y];
		// Convert to zero-based index and handle negative values (indicating players behind closed doors)
		return &Players[abs(pid) - 1];
	}

	return nullptr;
}

constexpr Direction16 Direction16Flip(Direction16 x, Direction16 pivot)
{
	std::underlying_type_t<Direction16> ret = (2 * static_cast<std::underlying_type_t<Direction16>>(pivot) + 16 - static_cast<std::underlying_type_t<Direction16>>(x)) % 16;

	return static_cast<Direction16>(ret);
}

void UpdateMissileVelocity(Missile &missile, Point destination, int velocityInPixels)
{
	missile.position.velocity = { 0, 0 };

	if (missile.position.tile == destination)
		return;

	// Get the normalized vector in isometric projection
	Displacement fixed16NormalVector = (missile.position.tile - destination).worldToNormalScreen();

	// Multiplying by the target velocity gives us a scaled velocity vector.
	missile.position.velocity = fixed16NormalVector * velocityInPixels;
}

/**
 * @brief Add the missile to the lookup tables
 * @param missile The missile to add
 */
void PutMissile(Missile &missile)
{
	Point position = missile.position.tile;

	if (!InDungeonBounds(position))
		missile._miDelFlag = true;

	if (missile._miDelFlag) {
		return;
	}

	DungeonFlag &flags = dFlags[position.x][position.y];
	flags |= DungeonFlag::Missile;
	if (missile._mitype == MissileID::FireWall)
		flags |= DungeonFlag::MissileFireWall;
	if (missile._mitype == MissileID::LightningWall)
		flags |= DungeonFlag::MissileLightningWall;

	if (missile._miPreFlag)
		MissilePreFlag = true;
}

void UpdateMissilePos(Missile &missile)
{
	Displacement pixelsTravelled = missile.position.traveled >> 16;

	Displacement tileOffset = pixelsTravelled.screenToMissile();
	missile.position.tile = missile.position.start + tileOffset;

	missile.position.offset = pixelsTravelled + tileOffset.worldToScreen();

	Displacement absoluteLightOffset = pixelsTravelled.screenToLight();
	ChangeLightOffset(missile._mlid, absoluteLightOffset - tileOffset * 8);
}

/**
 * @brief Dodgy hack used to correct the position for charging monsters.
 *
 * If the monster represented by this missile is *not* facing north in some way it gets shifted to the south.
 * This appears to compensate for some visual oddity or invalid calculation earlier in the ProcessRhino logic.
 * @param missile MissileStruct representing a charging monster.
 */
void MoveMissilePos(Missile &missile)
{
	Direction moveDirection;

	switch (static_cast<Direction>(missile._mimfnum)) {
	case Direction::East:
		moveDirection = Direction::SouthEast;
		break;
	case Direction::West:
		moveDirection = Direction::SouthWest;
		break;
	case Direction::South:
	case Direction::SouthWest:
	case Direction::SouthEast:
		moveDirection = Direction::South;
		break;
	default:
		return;
	}

	auto target = missile.position.tile + moveDirection;
	if (IsTileAvailable(*missile.sourceMonster(), target)) {
		missile.position.tile = target;
		missile.position.offset += Displacement(moveDirection).worldToScreen();
	}
}

int ProjectileMonsterDamage(Missile &missile)
{
	const Monster &monster = *missile.sourceMonster();
	return monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1);
}

int ProjectileTrapDamage(Missile &missile)
{
	return currlevel + GenerateRnd(2 * currlevel);
}

bool MonsterMHit(int pnum, int monsterId, int mindam, int maxdam, int dist, Missile missile, MissileID t, DamageType damageType, bool shift)
{
	auto &monster = Monsters[monsterId];

	if (!monster.isPossibleToHit() || monster.isImmune(missile, t, damageType))
		return false;

	int hit = GenerateRnd(100);
	int hper = 0;
	const Player &player = Players[pnum];
	const MissileData &missileData = GetMissileData(t);
	if (missileData.isArrow()) {
		hper = player.GetRangedPiercingToHit();
		hper -= player.CalculateArmorPierce(monster.armorClass, false);
		hper -= (dist * dist) / 2;
	} else {
		hper = player.GetMagicToHit() - (monster.level(sgGameInitInfo.nDifficulty) * 2) - dist;
	}

	hper = clamp(hper, 5, 95);

	if (monster.mode == MonsterMode::Petrified)
		hit = 0;

	if (monster.tryLiftGargoyle())
		return true;

	if (hit >= hper) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	int dam;
	dam = mindam + GenerateRnd(maxdam - mindam + 1);

	if (missileData.isArrow() && damageType == DamageType::Physical) {
		dam = player._pIBonusDamMod + dam * player._pIBonusDam / 100 + dam;
		if (player._pClass == HeroClass::Rogue)
			dam += player._pDamageMod;
		else
			dam += player._pDamageMod / 2;
		if (monster.data().monsterClass == MonsterClass::Demon && HasAnyOf(player._pIFlags, ItemSpecialEffect::TripleDemonDamage))
			dam *= 3;
	}
	bool resist = monster.isResistant(t, damageType);
	if (!shift)
		dam <<= 6;
	if (resist)
		dam >>= 2;

	if (&player == MyPlayer)
		ApplyMonsterDamage(damageType, monster, dam);

	if (monster.hitPoints >> 6 <= 0) {
		M_StartKill(monster, player);
	} else if (resist) {
		monster.tag(player);
		PlayEffect(monster, MonsterSound::Hit);
	} else {
		if (monster.mode != MonsterMode::Petrified && missileData.isArrow() && HasAnyOf(player._pIFlags, ItemSpecialEffect::Knockback)) {
			M_GetKnockback(monster);
		} else if (monster.mode != MonsterMode::Petrified && player.InvBody[INVLOC_HAND_LEFT]._itype == ItemType::Bow
		    && player.InvBody[INVLOC_HAND_LEFT]._iMagical == ITEM_QUALITY_UNIQUE) {
			if (HasAllOf(player._pIFlags, ItemSpecialEffect::FastestAttack | ItemSpecialEffect::Empower)        // Empowered Windforce
			    || HasAllOf(player._pIFlags, ItemSpecialEffect::MultipleArrows | ItemSpecialEffect::Empower)) { // Empowered Gnat Sting
				M_GetKnockback(monster);
				if (HasAllOf(player._pIFlags, ItemSpecialEffect::FastestAttack | ItemSpecialEffect::Empower)) { // Empowered Windforce
					AddMissile(monster.position.old, { 2, 0 }, Direction::South, MissileID::WeaponExplosion, TARGET_MONSTERS, player.getId(), 0, 0);
				}
			}
		}
		if (monster.type().type != MT_GOLEM)
			M_StartHit(monster, player, dam);
	}

	if (monster.activeForTicks == 0) {
		monster.activeForTicks = UINT8_MAX;
		monster.position.last = player.position.tile;
	}

	return true;
}

bool Plr2PlrMHit(const Player &player, int p, int mindam, int maxdam, int dist, MissileID mtype, DamageType damageType, bool shift, bool *blocked)
{
	Player &target = Players[p];

	if (sgGameInitInfo.bFriendlyFire == 0 && player.friendlyMode)
		return false;

	*blocked = false;

	if (target.isOnArenaLevel() && target._pmode == PM_WALK_SIDEWAYS)
		return false;

	if (target._pInvincible) {
		return false;
	}

	const MissileData &missileData = GetMissileData(mtype);

	if (HasAnyOf(target._pSpellFlags, SpellFlag::Etherealize) && missileData.isArrow()) {
		return false;
	}

	int8_t resper;
	switch (damageType) {
	case DamageType::Fire:
		resper = target._pFireResist;
		break;
	case DamageType::Lightning:
		resper = target._pLghtResist;
		break;
	case DamageType::Magic:
	case DamageType::Acid:
		resper = target._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}

	int hper = GenerateRnd(100);

	int hit;
	if (missileData.isArrow()) {
		hit = player.GetRangedPiercingToHit()
		    - player.CalculateArmorPierce(target.GetArmor(), false)
		    - (dist * dist / 2)
		    - target.GetArmor();
	} else {
		hit = player.GetMagicToHit()
		    - (target._pLevel * 2)
		    - dist;
	}

	hit = clamp(hit, 5, 95);

	if (hper >= hit) {
		return false;
	}

	int blkper = 100;
	if (!shift && (target._pmode == PM_STAND || target._pmode == PM_ATTACK) && target._pBlockFlag) {
		blkper = GenerateRnd(100);
	}

	int blk = target.GetBlockChance() - (player._pLevel * 2);
	blk = clamp(blk, 0, 100);

	int dam;
	dam = mindam + GenerateRnd(maxdam - mindam + 1);
	if (missileData.isArrow() && damageType == DamageType::Physical)
		dam += player._pIBonusDamMod + player._pDamageMod + dam * player._pIBonusDam / 100;
	if (!shift)
		dam <<= 6;

	if (!missileData.isArrow())
		dam /= 2;
	if (resper > 0) {
		dam -= (dam * resper) / 100;
		if (&player == MyPlayer)
			NetSendCmdDamage(true, p, dam, damageType);
		target.Say(HeroSpeech::ArghClang);
		return true;
	}

	if (blkper < blk) {
		StartPlrBlock(target, GetDirection(target.position.tile, player.position.tile));
		*blocked = true;
	} else {
		if (&player == MyPlayer)
			NetSendCmdDamage(true, p, dam, damageType);
		StartPlrHit(target, dam, false);
	}

	return true;
}

void RotateBlockedMissile(Missile &missile)
{
	int rotation = PickRandomlyAmong({ -1, 1 });

	if (missile._miAnimType == MissileGraphicID::Arrow) {
		int dir = missile._miAnimFrame + rotation;
		missile._miAnimFrame = (dir + 15) % 16 + 1;
		return;
	}

	int dir = missile._mimfnum + rotation;
	int mAnimFAmt = GetMissileSpriteData(missile._miAnimType).animFAmt;
	if (dir < 0)
		dir = mAnimFAmt - 1;
	else if (dir >= mAnimFAmt)
		dir = 0;

	SetMissDir(missile, dir);
}

void CheckMissileCol(Missile &missile, DamageType damageType, int minDamage, int maxDamage, bool isDamageShifted, Point position, bool dontDeleteOnCollision)
{
	if (!InDungeonBounds(position))
		return;

	int mx = position.x;
	int my = position.y;

	bool isMonsterHit = false;
	int mid = dMonster[mx][my];
	if (mid > 0 || (mid != 0 && Monsters[abs(mid) - 1].mode == MonsterMode::Petrified)) {
		mid = abs(mid) - 1;
		if (missile.IsTrap()
		    || (missile._micaster == TARGET_PLAYERS && (                                           // or was fired by a monster and
		            Monsters[mid].isPlayerMinion() != Monsters[missile._misource].isPlayerMinion() //  the monsters are on opposing factions
		            || (Monsters[missile._misource].flags & MFLAG_BERSERK) != 0                    //  or the attacker is berserked
		            || (Monsters[mid].flags & MFLAG_BERSERK) != 0                                  //  or the target is berserked
		            ))) {
			// then the missile can potentially hit this target
			isMonsterHit = MonsterTrapHit(mid, minDamage, maxDamage, missile._midist, missile._mitype, damageType, isDamageShifted);
		} else if (IsAnyOf(missile._micaster, TARGET_BOTH, TARGET_MONSTERS)) {
			isMonsterHit = MonsterMHit(missile._misource, mid, minDamage, maxDamage, missile._midist, missile, missile._mitype, damageType, isDamageShifted);
		}
	}

	if (isMonsterHit) {
		if (!dontDeleteOnCollision)
			missile._mirange = 0;
		missile._miHitFlag = true;
	}

	bool isPlayerHit = false;
	bool blocked = false;
	const int8_t pid = dPlayer[mx][my];
	if (pid > 0) {
		if (missile._micaster != TARGET_BOTH && !missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				if ((pid - 1) != missile._misource)
					isPlayerHit = Plr2PlrMHit(Players[missile._misource], pid - 1, minDamage, maxDamage, missile._midist, missile._mitype, damageType, isDamageShifted, &blocked);
			} else {
				Monster &monster = Monsters[missile._misource];
				isPlayerHit = PlayerMHit(pid - 1, &monster, missile._midist, minDamage, maxDamage, missile._mitype, damageType, isDamageShifted, DeathReason::MonsterOrTrap, &blocked);
			}
		} else {
			DeathReason deathReason = (!missile.IsTrap() && (missile._miAnimType == MissileGraphicID::FireWall || missile._miAnimType == MissileGraphicID::Lightning)) ? DeathReason::Player : DeathReason::MonsterOrTrap;
			isPlayerHit = PlayerMHit(pid - 1, nullptr, missile._midist, minDamage, maxDamage, missile._mitype, damageType, isDamageShifted, deathReason, &blocked);
		}
	}

	if (isPlayerHit) {
		missile._miHitFlag = true;
	}

	if (isPlayerHit) {
		if (gbIsHellfire && blocked) {
			RotateBlockedMissile(missile);
		} else if (!dontDeleteOnCollision) {
			missile._mirange = 0;
		}
		missile._miHitFlag = true;
	}

	if (IsMissileBlockedByTile({ mx, my })) {
		Object *object = FindObjectAtPosition({ mx, my });
		if (object != nullptr && object->IsBreakable()) {
			BreakObjectMissile(missile.sourcePlayer(), *object);
		}

		if (!dontDeleteOnCollision)
			missile._mirange = 0;
		missile._miHitFlag = false;
	}

	const MissileData &missileData = GetMissileData(missile._mitype);
	if (missile._mirange == 0 && missileData.miSFX != -1)
		PlaySfxLoc(missileData.miSFX, missile.position.tile);
}

bool MoveMissile(Missile &missile, tl::function_ref<bool(Point)> checkTile, bool ifCheckTileFailsDontMoveToTile = false)
{
	Point prevTile = missile.position.tile;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);

	int possibleVisitTiles;
	if (missile.position.velocity.deltaX == 0 || missile.position.velocity.deltaY == 0)
		possibleVisitTiles = prevTile.WalkingDistance(missile.position.tile);
	else
		possibleVisitTiles = prevTile.ManhattanDistance(missile.position.tile);

	if (possibleVisitTiles == 0)
		return false;

	// Did the missile skip a tile?
	if (possibleVisitTiles > 1) {
		auto speed = abs(missile.position.velocity);
		float denominator = (2 * speed.deltaY >= speed.deltaX) ? 2 * speed.deltaY : speed.deltaX;
		auto incVelocity = missile.position.velocity * ((32 << 16) / denominator);
		auto traveled = missile.position.traveled - missile.position.velocity;
		// Adjust the traveled vector to start on the next smallest multiple of incVelocity
		if (incVelocity.deltaY != 0)
			traveled.deltaY = (traveled.deltaY / incVelocity.deltaY) * incVelocity.deltaY;
		if (incVelocity.deltaX != 0)
			traveled.deltaX = (traveled.deltaX / incVelocity.deltaX) * incVelocity.deltaX;
		do {
			auto initialDiff = missile.position.traveled - traveled;
			traveled += incVelocity;
			auto incDiff = missile.position.traveled - traveled;

			// we are at the original calculated position => resume with normal logic
			if ((initialDiff.deltaX < 0) != (incDiff.deltaX < 0))
				break;
			if ((initialDiff.deltaY < 0) != (incDiff.deltaY < 0))
				break;

			// calculate in-between tile
			Displacement pixelsTraveled = traveled >> 16;
			Displacement tileOffset = pixelsTraveled.screenToMissile();
			Point tile = missile.position.start + tileOffset;

			// we haven't quite reached the missile's current position,
			// but we can break early to avoid checking collisions in this tile twice
			if (tile == missile.position.tile)
				break;

			// skip collision logic if the missile is on a corner between tiles
			if (pixelsTraveled.deltaY % 16 == 0
			    && pixelsTraveled.deltaX % 32 == 0
			    && abs(pixelsTraveled.deltaY / 16) % 2 != abs(pixelsTraveled.deltaX / 32) % 2) {
				continue;
			}

			// don't call checkTile more than once for a tile
			if (prevTile == tile)
				continue;

			prevTile = tile;

			if (!checkTile(tile)) {
				missile.position.traveled = traveled;
				if (ifCheckTileFailsDontMoveToTile) {
					missile.position.traveled -= incVelocity;
					UpdateMissilePos(missile);
					missile.position.StopMissile();
				} else {
					UpdateMissilePos(missile);
				}
				return true;
			}

		} while (true);
	}

	if (!checkTile(missile.position.tile) && ifCheckTileFailsDontMoveToTile) {
		missile.position.traveled -= missile.position.velocity;
		UpdateMissilePos(missile);
		missile.position.StopMissile();
	}

	return true;
}

void MoveMissileAndCheckMissileCol(Missile &missile, DamageType damageType, int mindam, int maxdam, bool ignoreStart, bool ifCollidesDontMoveToHitTile)
{
	auto checkTile = [&](Point tile) {
		if (ignoreStart && missile.position.start == tile)
			return true;

		CheckMissileCol(missile, damageType, mindam, maxdam, false, tile, false);

		// Did missile hit anything?
		if (missile._mirange != 0)
			return true;

		if (missile._miHitFlag && GetMissileData(missile._mitype).movementDistribution == MissileMovementDistribution::Blockable)
			return false;

		return !IsMissileBlockedByTile(tile);
	};

	bool tileChanged = MoveMissile(missile, checkTile, ifCollidesDontMoveToHitTile);

	int16_t tileTargetHash = dMonster[missile.position.tile.x][missile.position.tile.y] ^ dPlayer[missile.position.tile.x][missile.position.tile.y];

	// missile didn't change the tile... check that we perform CheckMissileCol only once for any monster/player to avoid multiple hits for slow missiles
	if (!tileChanged && missile.lastCollisionTargetHash != tileTargetHash) {
		CheckMissileCol(missile, damageType, mindam, maxdam, false, missile.position.tile, false);
	}

	// remember what target CheckMissileCol was checked against
	missile.lastCollisionTargetHash = tileTargetHash;
}

void SetMissAnim(Missile &missile, MissileGraphicID animtype)
{
	int dir = missile._mimfnum;

	if (animtype > MissileGraphicID::None) {
		animtype = MissileGraphicID::None;
	}

	const MissileFileData &missileData = GetMissileSpriteData(animtype);

	missile._miAnimType = animtype;
	missile._miAnimFlags = missileData.flags;
	if (!HeadlessMode) {
		missile._miAnimData = missileData.spritesForDirection(static_cast<size_t>(dir));
	}
	missile._miAnimDelay = missileData.animDelay(dir);
	missile._miAnimLen = missileData.animLen(dir);
	missile._miAnimWidth = missileData.animWidth;
	missile._miAnimWidth2 = missileData.animWidth2;
	missile._miAnimCnt = 0;
	missile._miAnimFrame = 1;
}

void AddRune(Missile &missile, Point dst, MissileID missileID)
{
	if (LineClearMissile(missile.position.start, dst)) {
		std::optional<Point> runePosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }
			    if (TileHasAny(dPiece[target.x][target.y], TileProperties::Solid)) {
				    return false;
			    }
			    return true;
		    },
		    dst, 0, 9);

		if (runePosition) {
			missile.position.tile = *runePosition;
			missile.var1 = static_cast<int8_t>(missileID);
			missile._mlid = AddLight(missile.position.tile, 8);
			return;
		}
	}

	missile._miDelFlag = true;
}

bool CheckIfTrig(Point position)
{
	for (int i = 0; i < numtrigs; i++) {
		if (trigs[i].position.WalkingDistance(position) < 2)
			return true;
	}
	return false;
}

bool GuardianTryFireAt(Missile &missile, Point target)
{
	Point position = missile.position.tile;

	if (!LineClearMissile(position, target))
		return false;
	int mid = dMonster[target.x][target.y] - 1;
	if (mid < 0)
		return false;
	const Monster &monster = Monsters[mid];
	if (monster.isPlayerMinion())
		return false;
	if (monster.hitPoints >> 6 <= 0)
		return false;

	Player &player = Players[missile._misource];
	int baseDamage = (player._pLevel / 2) + 1;
	int minDmg = ScaleSpellEffect(baseDamage, missile._mispllvl);
	int maxDmg = ScaleSpellEffect((baseDamage + 9), missile._mispllvl);
	int dmg = GenerateRnd(maxDmg - minDmg + 1) + minDmg; // generates a number between minDmg and maxDmg

	Direction dir = GetDirection(position, target);
	AddMissile(position, target, dir, MissileID::Firebolt, TARGET_MONSTERS, missile._misource, dmg, missile.sourcePlayer()->GetSpellLevel(SpellID::Guardian), &missile);
	SetMissDir(missile, 2);
	missile.var2 = 3;

	return true;
}

bool GrowWall(int playerId, Point position, Point target, MissileID type, int spellLevel, int damage)
{
	int dp = dPiece[position.x][position.y];
	assert(dp <= MAXTILES && dp >= 0);

	if (TileHasAny(dp, TileProperties::BlockMissile) || !InDungeonBounds(target)) {
		return false;
	}

	AddMissile(position, position, Players[playerId]._pdir, type, TARGET_BOTH, playerId, damage, spellLevel);
	return true;
}

/** @brief Sync missile position with parent missile */
void SyncPositionWithParent(Missile &missile, const AddMissileParameter &parameter)
{
	const Missile *parent = parameter.pParent;
	if (parent == nullptr)
		return;

	missile.position.offset = parent->position.offset;
	missile.position.traveled = parent->position.traveled;
}

void SpawnLightning(Missile &missile, int dam)
{
	missile._mirange--;
	MoveMissile(
	    missile, [&](Point tile) {
		    assert(InDungeonBounds(tile));
		    int pn = dPiece[tile.x][tile.y];
		    assert(pn >= 0 && pn <= MAXTILES);

		    if (!missile.IsTrap() || tile != missile.position.start) {
			    if (TileHasAny(pn, TileProperties::BlockMissile)) {
				    missile._mirange = 0;
				    return false;
			    }
		    }

		    return true;
	    });

	auto position = missile.position.tile;
	int pn = dPiece[position.x][position.y];
	if (!TileHasAny(pn, TileProperties::BlockMissile)) {
		if (position != Point { missile.var1, missile.var2 } && InDungeonBounds(position)) {
			MissileID type = MissileID::Lightning;
			if (missile.sourceType() == MissileSource::Monster
			    && IsAnyOf(missile.sourceMonster()->type().type, MT_STORM, MT_RSTORM, MT_STORML, MT_MAEL)) {
				type = MissileID::ThinLightning;
			}
			AddMissile(
			    position,
			    missile.position.start,
			    Direction::South,
			    type,
			    missile._micaster,
			    missile._misource,
			    dam,
			    missile._mispllvl,
			    &missile);
			missile.var1 = position.x;
			missile.var2 = position.y;
		}
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
	}
}

} // namespace

#ifdef BUILD_TESTING
void TestRotateBlockedMissile(Missile &missile)
{
	RotateBlockedMissile(missile);
}
#endif

bool IsMissileBlockedByTile(Point tile)
{
	if (!InDungeonBounds(tile)) {
		return true;
	}

	if (TileHasAny(dPiece[tile.x][tile.y], TileProperties::BlockMissile)) {
		return true;
	}

	Object *object = FindObjectAtPosition(tile);
	// _oMissFlag is true if the object allows missiles to pass through so we need to invert the check here...
	return object != nullptr && !object->_oMissFlag;
}

void GetDamageAmt(SpellID i, int *mind, int *maxd)
{
	assert(MyPlayer != nullptr);
	assert(i >= SpellID::FIRST && i <= SpellID::LAST);

	Player &myPlayer = *MyPlayer;

	const int sl = myPlayer.GetSpellLevel(i);

	switch (i) {
	case SpellID::Firebolt:
		*mind = (myPlayer._pMagic / 8) + sl + 1;
		*maxd = *mind + myPlayer._pLevel + 9;
		break;
	case SpellID::Healing:
	case SpellID::HealOther:
		/// BUGFIX: healing calculation is unused
		*mind = AddClassHealingBonus(myPlayer._pLevel + sl + 1, myPlayer._pClass) - 1;
		*maxd = AddClassHealingBonus((4 * myPlayer._pLevel) + (6 * sl) + 10, myPlayer._pClass) - 1;
		break;
	case SpellID::RuneOfLight:
	case SpellID::Lightning:
		*mind = 2;
		*maxd = 2 + myPlayer._pLevel;
		break;
	case SpellID::Flash: {
		int base = (myPlayer._pLevel * 4) + (sl * 10); // Player level and spell level scaling
		// The percentage of lightning damage to be used, capped at 100% at spell level 15 and above
		double lightningPct = std::min(0.1 * (1 + (sl - 1) / 2), 1.0);
		// Calculate the additional lightning damage
		int minLdam = static_cast<int>(lightningPct * myPlayer._pILMinDam);
		int maxLdam = static_cast<int>(lightningPct * myPlayer._pILMaxDam);
		// Calculate the total minimum and maximum damage
		*mind = (base / 2) + minLdam;
		*maxd = base + maxLdam;
	} break;
	case SpellID::Identify:
	case SpellID::TownPortal:
	case SpellID::StoneCurse:
	case SpellID::Infravision:
	case SpellID::Phasing:
	case SpellID::ManaShield:
	case SpellID::DoomSerpents:
	case SpellID::BloodRitual:
	case SpellID::Invisibility:
	case SpellID::Rage:
	case SpellID::Teleport:
	case SpellID::Etherealize:
	case SpellID::ItemRepair:
	case SpellID::StaffRecharge:
	case SpellID::TrapDisarm:
	case SpellID::Resurrect:
	case SpellID::Telekinesis:
	case SpellID::Warp:
	case SpellID::Reflect:
	case SpellID::Berserk:
	case SpellID::Search:
	case SpellID::RuneOfStone:
		*mind = -1;
		*maxd = -1;
		break;
	case SpellID::FireWall:
	case SpellID::LightningWall:
	case SpellID::RingOfFire:
		*mind = 2 * myPlayer._pLevel + 4;
		*maxd = *mind + 36;
		break;
	case SpellID::Fireball:
	case SpellID::RuneOfFire: {
		int base = (2 * myPlayer._pLevel) + 4;
		*mind = ScaleSpellEffect(base, sl);
		*maxd = ScaleSpellEffect(base + 36, sl);
	} break;
	case SpellID::Guardian: {
		int base = (myPlayer._pLevel / 2) + 1;
		*mind = ScaleSpellEffect(base, sl);
		*maxd = ScaleSpellEffect(base + 9, sl);
	} break;
	case SpellID::ChainLightning:
		*mind = 4;
		*maxd = 4 + (2 * myPlayer._pLevel);
		break;
	case SpellID::FlameWave:
		*mind = 6 * (myPlayer._pLevel + 1);
		*maxd = *mind + 54;
		break;
	case SpellID::Immolation:
	case SpellID::RuneOfImmolation:
	case SpellID::Nova:
	case SpellID::RuneOfNova:
		*mind = (ScaleSpellEffect((myPlayer._pLevel + 5) / 2, sl) * 5) / 6;
		*maxd = (ScaleSpellEffect((myPlayer._pLevel + 30) / 2, sl) * 5) / 6;
		break;
	case SpellID::Inferno:
		*mind = sl + myPlayer._pLevel / 2;
		*maxd = (sl * 2) + myPlayer._pLevel;
		*maxd += (*maxd / 2) + (myPlayer._pMagic / 2);
		break;
	case SpellID::Golem:
		*mind = 2 * (sl) + myPlayer._pLevel;
		*maxd = 2 * (sl * 2) + (myPlayer._pLevel * 2);
		break;
	case SpellID::Apocalypse:
		*mind = myPlayer._pLevel + (sl * 5);
		*maxd = (myPlayer._pLevel * 2) + (sl * 10) + std::min(150, myPlayer._pMagic);
		break;
	case SpellID::Elemental:
		*mind = ScaleSpellEffect(2 * myPlayer._pLevel + 4, sl);
		/// BUGFIX: add here '*mind /= 2;'
		*maxd = ScaleSpellEffect(2 * myPlayer._pLevel + 40, sl);
		/// BUGFIX: add here '*maxd /= 2;'
		break;
	case SpellID::ChargedBolt:
		*mind = 1;
		*maxd = *mind + (myPlayer._pMagic / 4) + myPlayer._pLevel;
		break;
	case SpellID::HolyBolt:
		*mind = myPlayer._pLevel + 9;
		*maxd = *mind + myPlayer._pLevel + 9;
		break;
	case SpellID::BloodStar: {
		int base = (sl * 3) + 4;
		*mind = (ScaleSpellEffect(base, sl) / 2);
		*maxd = (ScaleSpellEffect(base + 36, sl) / 2);
	} break;
	case SpellID::BoneSpirit: {
		int base = ((sl * 3) + (myPlayer._pMagic / 2) + 4);
		*mind = (ScaleSpellEffect(base, sl) / 2);
		*maxd = (ScaleSpellEffect(base + 36, sl) / 2);
	} break;
	default:
		break;
	}
}

Direction16 GetDirection16(Point p1, Point p2)
{
	Displacement offset = p2 - p1;
	Displacement absolute = abs(offset);

	bool flipY = offset.deltaX != absolute.deltaX;
	bool flipX = offset.deltaY != absolute.deltaY;

	bool flipMedian = false;
	if (absolute.deltaX > absolute.deltaY) {
		std::swap(absolute.deltaX, absolute.deltaY);
		flipMedian = true;
	}

	Direction16 ret = Direction16::South;
	if (3 * absolute.deltaX <= (absolute.deltaY * 2)) { // mx/my <= 2/3, approximation of tan(33.75)
		if (5 * absolute.deltaX < absolute.deltaY)      // mx/my < 0.2, approximation of tan(11.25)
			ret = Direction16::SouthWest;
		else
			ret = Direction16::South_SouthWest;
	}

	Direction16 medianPivot = Direction16::South;
	if (flipY) {
		ret = Direction16Flip(ret, Direction16::SouthWest);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthWest);
	}
	if (flipX) {
		ret = Direction16Flip(ret, Direction16::SouthEast);
		medianPivot = Direction16Flip(medianPivot, Direction16::SouthEast);
	}
	if (flipMedian)
		ret = Direction16Flip(ret, medianPivot);
	return ret;
}

bool MonsterTrapHit(int monsterId, int mindam, int maxdam, int dist, MissileID t, DamageType damageType, bool shift)
{
	auto &monster = Monsters[monsterId];

	if (!monster.isPossibleToHit() || monster.isImmuneTraps(t, damageType))
		return false;

	int hit = GenerateRnd(100);
	int hper = 90 - monster.armorClass - dist;
	hper = clamp(hper, 5, 95);
	if (monster.tryLiftGargoyle())
		return true;
	if (hit >= hper && monster.mode != MonsterMode::Petrified) {
#ifdef _DEBUG
		if (!DebugGodMode)
#endif
			return false;
	}

	bool resist = monster.isResistant(t, damageType);
	int dam = mindam + GenerateRnd(maxdam - mindam + 1);
	if (!shift)
		dam <<= 6;
	if (resist)
		dam /= 4;
	ApplyMonsterDamage(damageType, monster, dam);
#ifdef _DEBUG
	if (DebugGodMode)
		monster.hitPoints = 0;
#endif
	if (monster.hitPoints >> 6 <= 0) {
		MonsterDeath(monster, monster.direction, true);
	} else if (resist) {
		PlayEffect(monster, MonsterSound::Hit);
	} else if (monster.type().type != MT_GOLEM) {
		M_StartHit(monster, dam);
	}
	return true;
}

bool PlayerMHit(int pnum, Monster *monster, int dist, int mind, int maxd, MissileID mtype, DamageType damageType, bool shift, DeathReason deathReason, bool *blocked)
{
	*blocked = false;

	Player &player = Players[pnum];

	if (player._pHitPoints >> 6 <= 0) {
		return false;
	}

	if (player._pInvincible) {
		return false;
	}

	const MissileData &missileData = GetMissileData(mtype);

	if (HasAnyOf(player._pSpellFlags, SpellFlag::Etherealize) && missileData.isArrow()) {
		return false;
	}

	int hit = GenerateRnd(100);
#ifdef _DEBUG
	if (DebugGodMode)
		hit = 1000;
#endif
	int hper = 40;
	if (missileData.isArrow()) {
		int tac = player.GetArmor();
		if (player._pLevel > 19) {
			tac += (int)(rand() % player._pLevel + 20);
		} else {
			tac += (int)(rand() % player._pLevel + 1);
		}
		if (monster != nullptr) {
			hper = monster->toHit
			    + ((monster->level(sgGameInitInfo.nDifficulty) - player._pLevel) * 2)
			    + 30
			    - (dist * 2) - tac;
		} else {
			hper = 100 - (tac / 2) - (dist * 2);
		}
	} else if (monster != nullptr) {
		if (damageType == DamageType::Fire) {
			hper += (monster->level(sgGameInitInfo.nDifficulty) * 2) - ((player._pLevel * 2) + (player._pFireResist / 2)) - (dist * 2);
		} else if (damageType == DamageType::Lightning) {
			hper += (monster->level(sgGameInitInfo.nDifficulty) * 2) - ((player._pLevel * 2) + (player._pLghtResist / 2)) - (dist * 2);
		} else if (damageType == DamageType::Magic) {
			hper += (monster->level(sgGameInitInfo.nDifficulty) * 2) - ((player._pLevel * 2) + (player._pMagResist / 2)) - (dist * 2);
		}
	}

	int minhit = 10;
	if (currlevel == 14)
		minhit = 20;
	if (currlevel == 15)
		minhit = 25;
	if (currlevel == 16)
		minhit = 30;
	hper = std::max(hper, minhit);

	int blk = 100;
	if ((player._pmode == PM_STAND || player._pmode == PM_ATTACK) && player._pBlockFlag) {
		blk = GenerateRnd(100);
	}

	if (shift)
		blk = 100;
	if (mtype == MissileID::AcidPuddle)
		blk = 100;

	int blkper = player.GetBlockChance(false);
	if (monster != nullptr)
		blkper -= (monster->level(sgGameInitInfo.nDifficulty) - player._pLevel) * 2;
	blkper = clamp(blkper, 0, 100);

	int8_t resper;
	switch (damageType) {
	case DamageType::Fire:
		resper = player._pFireResist;
		break;
	case DamageType::Lightning:
		resper = player._pLghtResist;
		break;
	case DamageType::Magic:
	case DamageType::Acid:
		resper = player._pMagResist;
		break;
	default:
		resper = 0;
		break;
	}

	if (hit >= hper) {
		return false;
	}

	int dam;
	if (!shift) {
		dam = (mind << 6) + GenerateRnd(((maxd - mind) << 6) + 1);
		if (monster == nullptr)
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::HalfTrapDamage))
				dam /= 2;
		dam += player._pIGetHit * 64;
	} else {
		dam = mind + GenerateRnd(maxd - mind + 1);
		if (monster == nullptr)
			if (HasAnyOf(player._pIFlags, ItemSpecialEffect::HalfTrapDamage))
				dam /= 2;
		dam += player._pIGetHit;
	}
	dam = std::max(dam, 64);

	if (resper <= 0 && blk < blkper) {
		Direction dir = player._pdir;
		if (monster != nullptr) {
			dir = GetDirection(player.position.tile, monster->position.tile);
		}
		*blocked = true;
		StartPlrBlock(player, dir);
		return true;
	}

	if (resper > 0) {
		dam -= dam * resper / 100;
		if (&player == MyPlayer) {
			ApplyPlrDamage(damageType, player, 0, 0, dam, deathReason);
		}

		if (player._pHitPoints >> 6 > 0) {
			player.Say(HeroSpeech::ArghClang);
		}
		return true;
	}

	if (&player == MyPlayer) {
		ApplyPlrDamage(damageType, player, 0, 0, dam, deathReason);
	}

	if (player._pHitPoints >> 6 > 0) {
		StartPlrHit(player, dam, false);
	}

	return true;
}

void SetMissDir(Missile &missile, int dir)
{
	missile._mimfnum = dir;
	SetMissAnim(missile, missile._miAnimType);
}

void InitMissiles()
{
	Player &myPlayer = *MyPlayer;

	AutoMapShowItems = false;
	myPlayer._pSpellFlags &= ~SpellFlag::Etherealize;
	if (myPlayer._pInfraFlag) {
		for (auto &missile : Missiles) {
			if (missile._mitype == MissileID::Infravision) {
				if (missile.sourcePlayer() == MyPlayer)
					CalcPlrItemVals(myPlayer, true);
			}
		}
	}

	if (HasAnyOf(myPlayer._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown)) {
		myPlayer._pSpellFlags &= ~SpellFlag::RageActive;
		myPlayer._pSpellFlags &= ~SpellFlag::RageCooldown;
		for (auto &missile : Missiles) {
			if (missile._mitype == MissileID::Rage) {
				if (missile.sourcePlayer() == MyPlayer) {
					CalcPlrItemVals(myPlayer, true);
				}
			}
		}
	}

	Missiles.clear();
	for (int j = 0; j < MAXDUNY; j++) {
		for (int i = 0; i < MAXDUNX; i++) { // NOLINT(modernize-loop-convert)
			dFlags[i][j] &= ~(DungeonFlag::Missile | DungeonFlag::MissileFireWall | DungeonFlag::MissileLightningWall);
		}
	}
}

void AddOpenNest(Missile &missile, AddMissileParameter &parameter)
{
	for (int x : { 80, 81 }) {
		for (int y : { 62, 63 }) {
			AddMissile({ x, y }, { 80, 62 }, parameter.midir, MissileID::BigExplosion, missile._micaster, missile._misource, missile._midam, 0);
		}
	}
	missile._miDelFlag = true;
}

void AddRuneOfFire(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::BigExplosion);
}

void AddRuneOfLight(Missile &missile, AddMissileParameter &parameter)
{
	int lvl = (missile.sourceType() == MissileSource::Player) ? missile.sourcePlayer()->_pLevel : 0;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2);
	missile._midam = dmg;
	AddRune(missile, parameter.dst, MissileID::LightningWall);
}

void AddRuneOfNova(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::Nova);
}

void AddRuneOfImmolation(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::Immolation);
}

void AddRuneOfStone(Missile &missile, AddMissileParameter &parameter)
{
	AddRune(missile, parameter.dst, MissileID::StoneCurse);
}

void AddReflect(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._miDelFlag = true;

	if (missile.sourceType() != MissileSource::Player)
		return;

	Player &player = *missile.sourcePlayer();

	int add = (missile._mispllvl != 0 ? missile._mispllvl : 2) * player._pLevel;
	if (player.wReflections + add >= std::numeric_limits<uint16_t>::max())
		add = 0;
	player.wReflections += add;
	if (&player == MyPlayer)
		NetSendCmdParam1(true, CMD_SETREFLECT, player.wReflections);
}

void AddBerserk(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;
	parameter.spellFizzled = true;

	if (missile.sourceType() == MissileSource::Trap)
		return;

	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0)
			    return false;

		    const Monster &monster = Monsters[monsterId];
		    if (monster.isPlayerMinion())
			    return false;
		    if ((monster.flags & MFLAG_BERSERK) != 0)
			    return false;
		    if (monster.isUnique() || monster.ai == MonsterAIID::Diablo)
			    return false;
		    if (IsAnyOf(monster.mode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge))
			    return false;

		    return true;
	    },
	    parameter.dst, 0, 5);

	if (targetMonsterPosition) {
		auto &monster = Monsters[abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1];
		Player &player = *missile.sourcePlayer();
		const int slvl = player.GetSpellLevel(SpellID::Berserk);
		monster.flags |= MFLAG_BERSERK | MFLAG_GOLEM;
		monster.minDamage = (GenerateRnd(10) + 120) * monster.minDamage / 100 + slvl + player._pLevel;
		monster.maxDamage = (GenerateRnd(10) + 120) * monster.maxDamage / 100 + slvl + player._pLevel;
		monster.minDamageSpecial = (GenerateRnd(10) + 120) * monster.minDamageSpecial / 100 + slvl + player._pLevel;
		monster.maxDamageSpecial = (GenerateRnd(10) + 120) * monster.maxDamageSpecial / 100 + slvl + player._pLevel;
		int lightRadius = leveltype == DTYPE_NEST ? 9 : 3;
		monster.lightId = AddLight(monster.position.tile, lightRadius);
		parameter.spellFizzled = false;
	}
}

void AddHorkSpawn(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 8);
	missile._mirange = 9;
	missile.var1 = static_cast<int32_t>(parameter.midir);
	PutMissile(missile);
}

void AddJester(Missile &missile, AddMissileParameter &parameter)
{
	MissileID spell = MissileID::Firebolt;
	switch (GenerateRnd(10)) {
	case 0:
	case 1:
		spell = MissileID::Firebolt;
		break;
	case 2:
		spell = MissileID::Fireball;
		break;
	case 3:
		spell = MissileID::FireWallControl;
		break;
	case 4:
		spell = MissileID::Guardian;
		break;
	case 5:
		spell = MissileID::ChainLightning;
		break;
	case 6:
		spell = MissileID::TownPortal;
		break;
	case 7:
		spell = MissileID::Teleport;
		break;
	case 8:
		spell = MissileID::Apocalypse;
		break;
	case 9:
		spell = MissileID::StoneCurse;
		break;
	}
	Missile *randomMissile = AddMissile(missile.position.start, parameter.dst, parameter.midir, spell, missile._micaster, missile._misource, 0, missile._mispllvl);
	parameter.spellFizzled = randomMissile == nullptr;
	missile._miDelFlag = true;
}

void AddStealPotions(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Crawl(0, 2, [&](Displacement displacement) {
		Point target = missile.position.start + displacement;
		if (!InDungeonBounds(target))
			return false;
		int8_t pnum = dPlayer[target.x][target.y];
		if (pnum == 0)
			return false;
		Player &player = Players[abs(pnum) - 1];

		bool hasPlayedSFX = false;
		for (int si = 0; si < MaxBeltItems; si++) {
			Item &beltItem = player.SpdList[si];
			_item_indexes ii = IDI_NONE;
			if (beltItem._itype == ItemType::Misc) {
				if (FlipCoin())
					continue;
				switch (beltItem._iMiscId) {
				case IMISC_FULLHEAL:
					ii = ItemMiscIdIdx(IMISC_HEAL);
					break;
				case IMISC_HEAL:
				case IMISC_MANA:
					player.RemoveSpdBarItem(si);
					break;
				case IMISC_FULLMANA:
					ii = ItemMiscIdIdx(IMISC_MANA);
					break;
				case IMISC_REJUV:
					ii = ItemMiscIdIdx(PickRandomlyAmong({ IMISC_HEAL, IMISC_MANA }));
					break;
				case IMISC_FULLREJUV:
					switch (GenerateRnd(3)) {
					case 0:
						ii = ItemMiscIdIdx(IMISC_FULLMANA);
						break;
					case 1:
						ii = ItemMiscIdIdx(IMISC_FULLHEAL);
						break;
					default:
						ii = ItemMiscIdIdx(IMISC_REJUV);
						break;
					}
					break;
				default:
					continue;
				}
			}
			if (ii != IDI_NONE) {
				auto seed = beltItem._iSeed;
				InitializeItem(beltItem, ii);
				beltItem._iSeed = seed;
				beltItem._iStatFlag = true;
			}
			if (!hasPlayedSFX) {
				PlaySfxLoc(IS_POPPOP2, target);
				hasPlayedSFX = true;
			}
		}
		RedrawEverything();

		return false;
	});
	missile._miDelFlag = true;
}

void AddStealMana(Missile &missile, AddMissileParameter & /*parameter*/)
{
	std::optional<Point> trappedPlayerPosition = FindClosestValidPosition(
	    [](Point target) {
		    return InDungeonBounds(target) && dPlayer[target.x][target.y] != 0;
	    },
	    missile.position.start, 0, 2);

	if (trappedPlayerPosition) {
		Player &player = Players[abs(dPlayer[trappedPlayerPosition->x][trappedPlayerPosition->y]) - 1];

		player._pMana = 0;
		player._pManaBase = player._pMana + player._pMaxManaBase - player._pMaxMana;
		CalcPlrInv(player, false);
		RedrawComponent(PanelDrawComponent::Mana);
		PlaySfxLoc(TSFX_COW7, *trappedPlayerPosition);
	}

	missile._miDelFlag = true;
}

void AddSpectralArrow(Missile &missile, AddMissileParameter &parameter)
{
	if (missile.sourceType() == MissileSource::Player)
		const Player &player = *missile.sourcePlayer();

	missile._mirange = 1;
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
}

void AddWarp(Missile &missile, AddMissileParameter &parameter)
{
	int minDistanceSq = std::numeric_limits<int>::max();

	int id = missile._misource;
	Player &player = Players[id];
	Point tile = player.position.tile;

	for (int i = 0; i < numtrigs && i < MAXTRIGGERS; i++) {
		TriggerStruct *trg = &trigs[i];
		if (IsNoneOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABNEXTLVL, WM_DIABRTNLVL))
			continue;
		Point candidate = trg->position;
		auto getTriggerOffset = [](TriggerStruct *trg) {
			switch (leveltype) {
			case DTYPE_CATHEDRAL:
				if (setlevel && setlvlnum == SL_VILEBETRAYER)
					return Displacement { 1, 1 }; // Portal
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 1, 2 };
				return Displacement { 0, 1 }; // WM_DIABNEXTLVL
			case DTYPE_CATACOMBS:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL))
					return Displacement { 1, 1 };
				return Displacement { 0, 1 }; // WM_DIABRTNLVL, WM_DIABNEXTLVL
			case DTYPE_CAVES:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL))
					return Displacement { 0, 1 };
				return Displacement { 1, 0 }; // WM_DIABRTNLVL, WM_DIABNEXTLVL
			case DTYPE_HELL:
				return Displacement { 1, 0 };
			case DTYPE_NEST:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 0, 1 };
				return Displacement { 1, 0 }; // WM_DIABNEXTLVL
			case DTYPE_CRYPT:
				if (IsAnyOf(trg->_tmsg, WM_DIABTWARPUP, WM_DIABPREVLVL, WM_DIABRTNLVL))
					return Displacement { 1, 1 };
				return Displacement { 0, 1 }; // WM_DIABNEXTLVL
			case DTYPE_TOWN:
				app_fatal("invalid leveltype: DTYPE_TOWN");
			case DTYPE_NONE:
				app_fatal("leveltype not set");
			}
			app_fatal(StrCat("invalid leveltype", static_cast<int>(leveltype)));
		};
		const Displacement triggerOffset = getTriggerOffset(trg);
		candidate += triggerOffset;
		const Displacement off = Point { player.position.tile } - candidate;
		const int distanceSq = off.deltaY * off.deltaY + off.deltaX * off.deltaX;
		if (distanceSq < minDistanceSq) {
			minDistanceSq = distanceSq;
			tile = candidate;
		}
	}
	missile._mirange = 2;
	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    for (int i = 0; i < numtrigs; i++) {
			    if (trigs[i].position == target)
				    return false;
		    }
		    return PosOkPlayer(player, target);
	    },
	    tile, 0, 5);

	if (teleportDestination) {
		missile.position.tile = *teleportDestination;
	} else {
		// No valid teleport destination found
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
	}
}

void AddLightningWall(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255 * (missile._mispllvl + 1);
	switch (missile.sourceType()) {
	case MissileSource::Trap:
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
		break;
	case MissileSource::Player: {
		Player &player = *missile.sourcePlayer();
		missile.var1 = player.position.tile.x;
		missile.var2 = player.position.tile.y;
	} break;
	case MissileSource::Monster:
		assert(missile.sourceType() != MissileSource::Monster);
		break;
	}
}

void AddBigExplosion(Missile &missile, AddMissileParameter & /*parameter*/)
{
	if (missile.sourceType() == MissileSource::Player) {
		int dmg = 2 * (missile.sourcePlayer()->_pLevel + GenerateRndSum(10, 2)) + 4;
		dmg = ScaleSpellEffect(dmg, missile._mispllvl);

		missile._midam = dmg;

		const DamageType damageType = GetMissileData(missile._mitype).damageType();
		for (Point position : PointsInRectangleColMajor(Rectangle { missile.position.tile, 1 }))
			CheckMissileCol(missile, damageType, dmg, dmg, false, position, true);
	}
	missile._mlid = AddLight(missile.position.start, 8);
	SetMissDir(missile, 0);
	missile._mirange = missile._miAnimLen - 1;
}

void AddImmolation(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl, 34);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightningBow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255;
	if (missile._misource < 0) {
		missile.var1 = missile.position.start.x;
		missile.var2 = missile.position.start.y;
	} else {
		missile.var1 = Players[missile._misource].position.tile.x;
		missile.var2 = Players[missile._misource].position.tile.y;
	}
	missile._midam <<= 6;
}

void AddMana(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int manaAmount = (GenerateRnd(10) + 1) << 6;
	for (int i = 0; i < player._pLevel; i++) {
		manaAmount += (GenerateRnd(4) + 1) << 6;
	}
	for (int i = 0; i < missile._mispllvl; i++) {
		manaAmount += (GenerateRnd(6) + 1) << 6;
	}
	if (player._pClass == HeroClass::Sorcerer)
		manaAmount *= 2;
	if (player._pClass == HeroClass::Rogue || player._pClass == HeroClass::Bard)
		manaAmount += manaAmount / 2;
	player._pMana += manaAmount;
	if (player._pMana > player._pMaxMana)
		player._pMana = player._pMaxMana;
	player._pManaBase += manaAmount;
	if (player._pManaBase > player._pMaxManaBase)
		player._pManaBase = player._pMaxManaBase;
	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Mana);
}

void AddMagi(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	player._pMana = player._pMaxMana;
	player._pManaBase = player._pMaxManaBase;
	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Mana);
}

void AddRingOfFire(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mirange = 7;
}

void AddSearch(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	if (&player == MyPlayer)
		AutoMapShowItems = true;
	int lvl = 2;
	if (missile._misource >= 0)
		lvl = player._pLevel * 2;
	missile._mirange = lvl + 10 * missile._mispllvl + 245;

	for (auto &other : Missiles) {
		if (&other != &missile && missile.isSameSource(other) && other._mitype == MissileID::Search) {
			int r1 = missile._mirange;
			int r2 = other._mirange;
			if (r2 < INT_MAX - r1)
				other._mirange = r1 + r2;
			missile._miDelFlag = true;
			break;
		}
	}
}

void AddChargedBoltBow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	missile._mirnd = GenerateRnd(15) + 1;
	if (missile._micaster != TARGET_MONSTERS) {
		missile._midam = 15;
	}

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, 5);
	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int32_t>(parameter.midir);
	missile._mirange = 256;
}

void AddElementalArrow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		const Player &player = Players[missile._misource];
		if (player._pClass == HeroClass::Rogue)
			av += (player._pLevel) / 4;
		else if (IsAnyOf(player._pClass, HeroClass::Warrior, HeroClass::Bard))
			av += (player._pLevel) / 8;
		if (IsAnyOf(player._pClass, HeroClass::Rogue, HeroClass::Warrior, HeroClass::Bard))
			av -= 1;
	}
	UpdateMissileVelocity(missile, dst, av);

	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 5);
}

void AddArrow(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int av = 32;
	if (missile._micaster == TARGET_MONSTERS) {
		const Player &player = Players[missile._misource];

		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::RandomArrowVelocity)) {
			av = GenerateRnd(32) + 16;
		}
		if (player._pClass == HeroClass::Rogue)
			av += (player._pLevel - 1) / 4;
		else if (player._pClass == HeroClass::Warrior || player._pClass == HeroClass::Bard)
			av += (player._pLevel - 1) / 8;
	}
	UpdateMissileVelocity(missile, dst, av);
	missile._miAnimFrame = static_cast<int>(GetDirection16(missile.position.start, dst)) + 1;
	missile._mirange = 256;
}

void UpdateVileMissPos(Missile &missile, Point dst)
{
	for (int k = 1; k < 50; k++) {
		for (int j = -k; j <= k; j++) {
			int yy = j + dst.y;
			for (int i = -k; i <= k; i++) {
				int xx = i + dst.x;
				if (PosOkPlayer(*MyPlayer, { xx, yy })) {
					missile.position.tile = { xx, yy };
					return;
				}
			}
		}
	}
}

void AddPhasing(Missile &missile, AddMissileParameter &parameter)
{
	missile._mirange = 2;

	Player &player = Players[missile._misource];

	if (missile._micaster == TARGET_BOTH) {
		missile.position.tile = parameter.dst;
		if (!PosOkPlayer(player, parameter.dst))
			UpdateVileMissPos(missile, parameter.dst);
		return;
	}

	std::array<Point, 4 * 9> targets;

	int count = 0;
	for (int y = -6; y <= 6; y++) {
		for (int x = -6; x <= 6; x++) {
			if ((x >= -3 && x <= 3) || (y >= -3 && y <= 3))
				continue; // Skip center

			Point target = missile.position.start + Displacement { x, y };
			if (!PosOkPlayer(player, target))
				continue;

			targets[count] = target;
			count++;
		}
	}

	if (count == 0) {
		missile._miDelFlag = true;
		return;
	}

	missile.position.tile = targets[std::max<int32_t>(GenerateRnd(count), 0)];
}

void AddFirebolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 26;
	if (missile._micaster == TARGET_MONSTERS) {
		sp = 16;
		if (!missile.IsTrap()) {
			sp += std::min(missile._mispllvl * 2, 47);
		}
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			const Player &player = *missile.sourcePlayer();
			missile._midam = (player._pMagic / 8) + player._pLevel + missile._mispllvl + 1;
		} break;

		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
}

void AddMagmaBall(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile.position.traveled.deltaX += 3 * missile.position.velocity.deltaX;
	missile.position.traveled.deltaY += 3 * missile.position.velocity.deltaY;
	UpdateMissilePos(missile);
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0)
		missile._mirange = 256;
	else
		missile._mirange = 1;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player:
			// Not typically created by Players
			break;
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
}

void AddTeleport(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    return PosOkPlayer(player, target);
	    },
	    parameter.dst, 0, 5);

	if (teleportDestination) {
		missile.position.tile = *teleportDestination;
		missile.position.start = *teleportDestination;
		missile._mirange = 2;
	} else {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
	}
}

void AddNovaBall(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 255;
	const Point position { missile._misource < 0 ? missile.position.start : Point(Players[missile._misource].position.tile) };
	missile.var1 = position.x;
	missile.var2 = position.y;
}

void AddFireWall(Missile &missile, AddMissileParameter &parameter)
{
	missile._midam = GenerateRndSum(10, 2) + 2;
	missile._midam += missile._misource >= 0 ? Players[missile._misource]._pLevel : currlevel; // BUGFIX: missing parenthesis around ternary (fixed)
	missile._midam <<= 3;
	UpdateMissileVelocity(missile, parameter.dst, 16);
	int i = missile._mispllvl;
	missile._mirange = 10;
	if (i > 0)
		missile._mirange *= i + 1;
	if (missile._micaster == TARGET_PLAYERS || missile._misource < 0)
		missile._mirange += currlevel;
	missile._mirange *= 16;
	missile.var1 = missile._mirange - missile._miAnimLen;
}

void AddFireball(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (missile._micaster == TARGET_MONSTERS) {
		sp += std::min(missile._mispllvl * 2, 34);
		Player &player = Players[missile._misource];

		int dmg = 2 * (player._pLevel + GenerateRndSum(10, 2)) + 4;
		missile._midam = ScaleSpellEffect(dmg, missile._mispllvl);
	}
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddLightningControl(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	UpdateMissileVelocity(missile, parameter.dst, 32);
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mirange = 256;
}

void AddLightning(Missile &missile, AddMissileParameter &parameter)
{
	missile.position.start = parameter.dst;

	SyncPositionWithParent(missile, parameter);

	missile._miAnimFrame = GenerateRnd(8) + 1;

	if (missile._micaster == TARGET_PLAYERS || missile.IsTrap()) {
		if (missile.IsTrap() || Monsters[missile._misource].type().type == MT_FAMILIAR)
			missile._mirange = 8;
		else
			missile._mirange = 10;
	} else {
		missile._mirange = (missile._mispllvl / 2) + 6;
	}
	missile._mlid = AddLight(missile.position.tile, 4);
}

void AddMissileExplosion(Missile &missile, AddMissileParameter &parameter)
{
	if (missile._micaster != TARGET_MONSTERS && missile._misource >= 0) {
		switch (Monsters[missile._misource].type().type) {
		case MT_SUCCUBUS:
			SetMissAnim(missile, MissileGraphicID::BloodStarExplosion);
			break;
		case MT_SNOWWICH:
			SetMissAnim(missile, MissileGraphicID::BloodStarBlueExplosion);
			break;
		case MT_HLSPWN:
			SetMissAnim(missile, MissileGraphicID::BloodStarRedExplosion);
			break;
		case MT_SOLBRNR:
			SetMissAnim(missile, MissileGraphicID::BloodStarYellowExplosion);
			break;
		default:
			break;
		}
	}

	assert(parameter.pParent != nullptr); // AddMissileExplosion will always be called with a parent associated to the missile.
	auto &parent = *parameter.pParent;
	missile.position.tile = parent.position.tile;
	missile.position.start = parent.position.start;
	missile.position.offset = parent.position.offset;
	missile.position.traveled = parent.position.traveled;
	missile._mirange = missile._miAnimLen;
}

void AddWeaponExplosion(Missile &missile, AddMissileParameter &parameter)
{
	missile.var2 = parameter.dst.x;
	if (parameter.dst.x == 1 || parameter.dst.x == 4) {
		SetMissAnim(missile, MissileGraphicID::MagmaBallExplosion);
	} else if (parameter.dst.x == 2 || parameter.dst.x == 5) {
		SetMissAnim(missile, MissileGraphicID::ChargedBolt);
	} else if (parameter.dst.x == 3 || parameter.dst.x == 6) {
		SetMissAnim(missile, MissileGraphicID::HolyBoltExplosion);
	} else if (parameter.dst.x == 7) {
		SetMissAnim(missile, MissileGraphicID::FireWall);
	}
	missile._mirange = missile._miAnimLen - 1;
}

int HolyFireChancePlr(Player &attacker, Player &target)
{
	if (&target != MyPlayer) {
		if (target._pLevel < 10) {
			return 10; // 10% chance at level 1-9.
		} else if (target._pLevel < 20) {
			return 15; // 15% chance at level 10-19.
		} else if (target._pLevel < 30) {
			return 20; // 20% chance at level 20-29.
		} else if (target._pLevel < 40) {
			return 25; // 25% chance at level 30-39.
		} else {
			return 30; // 30% chance at level 40 and above.
		}
	}
	return 0;
}

void HolyFireDamagePlr(Player &attacker, Player &target)
{
	if (&attacker != MyPlayer && &target == MyPlayer) {
		if (attacker.position.tile.WalkingDistance(target.position.tile) < 2
		    && !attacker.friendlyMode) {
			if (HasAnyOf(target._pIFlags, ItemSpecialEffect::Thorns)) {
				int eMind;
				int eMaxd;
				eMind = target._pIFMinDam;
				eMaxd = target._pIFMaxDam;
				int mdam = GenerateRnd(eMaxd - eMind + 1) + eMind;
				mdam -= mdam * (attacker._pFireResist / 100);
				mdam = mdam << 6;
				NetSendCmdDamage(true, attacker.getId(), mdam, DamageType::Fire);
				NetSendAddMissile(attacker.position.tile, { 0, 0 }, Direction::South, static_cast<uint16_t>(MissileID::FireWall), static_cast<uint16_t>(TARGET_MONSTERS), target.getId(), 0, 0);
			}
		}
	}
}

void CastHolyShockPlr(Player &attacker, Player &target)
{
	if (&attacker != MyPlayer && &target == MyPlayer) {
		SpellID spellId = SpellID::Flash;
		if (attacker.position.tile.WalkingDistance(target.position.tile) < 2
		    && !attacker.friendlyMode) {
			if (target._pRSpell != spellId || target._pRSplType != SpellType::Spell) {
				return;
			}
			if (target._pSplLvl[static_cast<int>(spellId)] <= 0) {
				return;
			}
			int spellLevel = target._pSplLvl[static_cast<int>(spellId)];
			PlaySFX(IS_CAST4);
			int base = (target._pLevel * 4) + (spellLevel * 10);
			double lightningPct = std::min(0.1 * (1 + (spellLevel - 1) / 2), 1.0);
			int lightningDamage = target._pILMinDam + GenerateRnd(target._pILMaxDam - target._pILMinDam + 1);
			lightningDamage = static_cast<int>(lightningPct * lightningDamage);
			int mdam = ((base / 2) + GenerateRnd((base / 2) + 1)) + lightningDamage;
			int bsmdam = mdam;
			bsmdam -= bsmdam * (attacker._pLghtResist / 100);
			bsmdam = bsmdam << 6;
			NetSendCmdDamage(true, attacker.getId(), bsmdam, DamageType::Lightning);
			NetSendAddMissile(target.position.tile, target.position.temp, target._pdir, static_cast<uint16_t>(MissileID::FlashBottom), static_cast<uint16_t>(TARGET_MONSTERS), target.getId(), mdam, spellLevel);
			NetSendAddMissile(target.position.tile, target.position.temp, target._pdir, static_cast<uint16_t>(MissileID::FlashTop), static_cast<uint16_t>(TARGET_MONSTERS), target.getId(), mdam, spellLevel);
		}
	}
}

void ExplodingBoneArmorPlr(Player &attacker, Player &target)
{
	if (&attacker != MyPlayer && &target == MyPlayer) {
		if (attacker.position.tile.WalkingDistance(target.position.tile) < 2
		    && !attacker.friendlyMode) {
			int eMind = target._pIMMinDam;
			int eMaxd = target._pIMMaxDam;
			int mdam = GenerateRnd(eMaxd - eMind + 1) + eMind;
			if (!target.InvBody[INVLOC_CHEST].isEmpty()) {
				auto &BoneArmor = target.InvBody[INVLOC_CHEST];
				if (HasAllOf(BoneArmor._iFlags, ItemSpecialEffect::MagicDamage | ItemSpecialEffect::FastestHitRecovery)
				    && HasAnyOf(target._pIFlags, ItemSpecialEffect::Empower)) {
					int arrows = 6;
					int directionIndex = static_cast<int>(target._pdir);
					std::array<int, 6> strafePattern6 = { 2, 1, 0, -1, -2, -3 };
					for (int arrow = 0; arrow < arrows; arrow++) {
						int arrowDirectionIndex;
						if (arrows == 6) {
							arrowDirectionIndex = (directionIndex + strafePattern6[arrow] + 8) % 8;
						}
						if (arrowDirectionIndex < 0) {
							arrowDirectionIndex += 8;
						}
						Direction arrowDirection = static_cast<Direction>(arrowDirectionIndex);
						Displacement displacement(arrowDirection);
						NetSendAddMissile(target.position.tile, target.position.old + displacement,
						    arrowDirection, static_cast<uint16_t>(MissileID::BoneSpirit), static_cast<uint16_t>(TARGET_MONSTERS), target.getId(), mdam, 0);
					}
				}
			}
		}
	}
}

int HolyFireChance(Player &player)
{
	if (&player == MyPlayer) {
		if (player._pLevel < 10) {
			return 10; // 10% chance at level 1-9.
		} else if (player._pLevel < 20) {
			return 15; // 15% chance at level 10-19.
		} else if (player._pLevel < 30) {
			return 20; // 20% chance at level 20-29.
		} else if (player._pLevel < 40) {
			return 25; // 25% chance at level 30-39.
		} else {
			return 30; // 30% chance at level 40 and above.
		}
	}
	return 0;
}

void HolyFireDamage(Player &player, Monster &monster)
{
	if (monster.position.tile.WalkingDistance(player.position.tile) < 2) {
		if (HasAnyOf(player._pIFlags, ItemSpecialEffect::Thorns) && monster.mode != MonsterMode::Death) {
			int eMind = player._pIFMinDam;
			int eMaxd = player._pIFMaxDam;
			int mdam = GenerateRnd(eMaxd - eMind + 1) + eMind;
			int res = monster.resistance & (RESIST_MAGIC | RESIST_FIRE | RESIST_LIGHTNING | IMMUNE_MAGIC | IMMUNE_FIRE | IMMUNE_LIGHTNING);
			if ((res & (RESIST_FIRE | IMMUNE_FIRE)) != 0) {
				mdam -= mdam / 2;
				mdam -= mdam / 2;
			}
			mdam = mdam << 6;
			ApplyMonsterDamage(DamageType::Fire, monster, mdam);
			NetSendAddMissile(monster.position.tile, { 0, 0 }, Direction::South, static_cast<uint16_t>(MissileID::FireWall), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), 0, 0);
			if (monster.hitPoints >> 6 <= 0)
				M_StartKill(monster, player);
			else
				M_StartHit(monster, player, mdam);
		}
	}
}

void CastHolyShock(Player &player, Monster &monster)
{
	SpellID spellId = SpellID::Flash;
	if (monster.position.tile.WalkingDistance(player.position.tile) < 2) {
		if (player._pRSpell != spellId || player._pRSplType != SpellType::Spell) {
			return;
		}
		if (player._pSplLvl[static_cast<int>(spellId)] <= 0) {
			return;
		}
		if (monster.mode == MonsterMode::Death) {
			return;
		}
		int spellLevel = player._pSplLvl[static_cast<int>(spellId)];
		int base = (player._pLevel * 4) + (spellLevel * 10);
		double lightningPct = std::min(0.1 * (1 + (spellLevel - 1) / 2), 1.0);
		int lightningDamage = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
		lightningDamage = static_cast<int>(lightningPct * lightningDamage);
		int mdam = ((base / 2) + GenerateRnd((base / 2) + 1)) + lightningDamage;
		int bsmdam = mdam << 6;
		ApplyMonsterDamage(DamageType::Lightning, monster, bsmdam);
		NetSendAddMissile(player.position.tile, player.position.temp, player._pdir, static_cast<uint16_t>(MissileID::FlashBottom), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), mdam, spellLevel);
		NetSendAddMissile(player.position.tile, player.position.temp, player._pdir, static_cast<uint16_t>(MissileID::FlashTop), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), mdam, spellLevel);
		if (monster.hitPoints >> 6 <= 0)
			M_StartKill(monster, player);
		else
			M_StartHit(monster, player, mdam);
	}
}

void ExplodingBoneArmor(Player &player, Monster &monster)
{
	if (monster.position.tile.WalkingDistance(player.position.tile) < 2) {
		int eMind = player._pIMMinDam;
		int eMaxd = player._pIMMaxDam;
		int mdam = GenerateRnd(eMaxd - eMind + 1) + eMind;
		if (!player.InvBody[INVLOC_CHEST].isEmpty()) {
			auto &BoneArmor = player.InvBody[INVLOC_CHEST];
			if (HasAllOf(BoneArmor._iFlags, ItemSpecialEffect::MagicDamage | ItemSpecialEffect::FastestHitRecovery)
			    && HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
				int arrows = 6;
				int directionIndex = static_cast<int>(player._pdir);
				std::array<int, 6> strafePattern6 = { 2, 1, 0, -1, -2, -3 };
				for (int arrow = 0; arrow < arrows; arrow++) {
					int arrowDirectionIndex;
					if (arrows == 6) {
						arrowDirectionIndex = (directionIndex + strafePattern6[arrow] + 8) % 8;
					}
					if (arrowDirectionIndex < 0) {
						arrowDirectionIndex += 8;
					}
					Direction arrowDirection = static_cast<Direction>(arrowDirectionIndex);
					Displacement displacement(arrowDirection);
					NetSendAddMissile(player.position.tile, player.position.old + displacement, arrowDirection,
					    static_cast<uint16_t>(MissileID::BoneSpirit), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), mdam, 0);
					if (monster.hitPoints >> 6 <= 0)
						M_StartKill(monster, player);
					else
						M_StartHit(monster, player, mdam);
				}
			}
		}
	}
}

void AddTownPortal(Missile &missile, AddMissileParameter &parameter)
{
	if (leveltype == DTYPE_TOWN) {
		missile.position.tile = parameter.dst;
		missile.position.start = parameter.dst;
	} else {
		std::optional<Point> targetPosition = FindClosestValidPosition(
		    [](Point target) {
			    if (!InDungeonBounds(target)) {
				    return false;
			    }
			    if (IsObjectAtPosition(target)) {
				    return false;
			    }
			    if (dPlayer[target.x][target.y] != 0) {
				    return false;
			    }
			    if (TileContainsMissile(target)) {
				    return false;
			    }

			    int dp = dPiece[target.x][target.y];
			    if (TileHasAny(dp, TileProperties::Solid | TileProperties::BlockMissile)) {
				    return false;
			    }
			    return !CheckIfTrig(target);
		    },
		    parameter.dst, 0, 5);

		if (targetPosition) {
			missile.position.tile = *targetPosition;
			missile.position.start = *targetPosition;
			missile._miDelFlag = false;
		} else {
			missile._miDelFlag = true;
		}
	}

	missile._mirange = 100;
	missile.var1 = missile._mirange - missile._miAnimLen;
	for (auto &other : Missiles) {
		if (other._mitype == MissileID::TownPortal && &other != &missile && missile.isSameSource(other))
			other._mirange = 0;
	}
	PutMissile(missile);
	if (missile.sourcePlayer() == MyPlayer && !missile._miDelFlag && leveltype != DTYPE_TOWN) {
		if (!setlevel) {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, currlevel, leveltype, 0);
		} else {
			NetSendCmdLocParam3(true, CMD_ACTIVATEPORTAL, missile.position.tile, setlvlnum, leveltype, 1);
		}
	}
}

void AddFlashBottom(Missile &missile, AddMissileParameter & /*parameter*/)
{
	switch (missile.sourceType()) {
	case MissileSource::Player: {
		Player &player = *missile.sourcePlayer();
		int sl = missile._mispllvl;
		int base = (player._pLevel * 4) + (sl * 10);
		double lightningPct = std::min(0.1 * (1 + (sl - 1) / 2), 1.0);
		int lightningDamage = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
		lightningDamage = static_cast<int>(lightningPct * lightningDamage);
		missile._midam = ((base / 2) + GenerateRnd((base / 2) + 1)) + lightningDamage;
	} break;
	case MissileSource::Monster:
		missile._midam = missile.sourceMonster()->level(sgGameInitInfo.nDifficulty) * 2;
		break;
	case MissileSource::Trap:
		missile._midam = currlevel / 2;
		break;
	}

	missile._mirange = 19;
}

void AddFlashTop(Missile &missile, AddMissileParameter & /*parameter*/)
{
	if (missile._micaster == TARGET_MONSTERS) {
		if (!missile.IsTrap()) {
			Player &player = Players[missile._misource];
			int sl = missile._mispllvl;
			int base = (player._pLevel * 4) + (sl * 10);
			double lightningPct = std::min(0.1 * (1 + (sl - 1) / 2), 1.0);
			int lightningDamage = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
			lightningDamage = static_cast<int>(lightningPct * lightningDamage);
			missile._midam = ((base / 2) + GenerateRnd((base / 2) + 1)) + lightningDamage;
		} else {
			missile._midam = currlevel / 2;
		}
	}
	missile._miPreFlag = true;
	missile._mirange = 19;
}

void AddManaShield(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	Player &player = Players[missile._misource];

	if (player.pManaShield) {
		parameter.spellFizzled = true;
		return;
	}

	player.pManaShield = true;
	if (&player == MyPlayer)
		NetSendCmd(true, CMD_SETSHIELD);
}

void AddFlameWave(Missile &missile, AddMissileParameter &parameter)
{
	missile._midam = GenerateRnd(10) + Players[missile._misource]._pLevel + 1;
	UpdateMissileVelocity(missile, parameter.dst, 16);
	missile._mirange = 255;

	// Adjust missile's position for rendering
	missile.position.tile += Direction::South;
	missile.position.offset.deltaY -= 32;
}

void AddGuardian(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	std::optional<Point> spawnPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }
		    if (dMonster[target.x][target.y] != 0) {
			    return false;
		    }
		    if (IsObjectAtPosition(target)) {
			    return false;
		    }
		    if (TileContainsMissile(target)) {
			    return false;
		    }

		    int dp = dPiece[target.x][target.y];
		    if (TileHasAny(dp, TileProperties::Solid | TileProperties::BlockMissile)) {
			    return false;
		    }

		    return LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spawnPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	missile._miDelFlag = false;
	missile.position.tile = *spawnPosition;
	missile.position.start = *spawnPosition;

	missile._mlid = AddLight(missile.position.tile, 1);
	missile._mirange = missile._mispllvl + (player._pLevel / 2);

	if (missile._mirange > 30)
		missile._mirange = 30;
	missile._mirange <<= 4;
	if (missile._mirange < 30)
		missile._mirange = 30;

	missile.var1 = missile._mirange - missile._miAnimLen;
	missile.var3 = 1;
}

void AddChainLightning(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile._mirange = 1;
}

namespace {
void InitMissileAnimationFromMonster(Missile &mis, Direction midir, const Monster &mon, MonsterGraphic graphic)
{
	const AnimStruct &anim = mon.type().getAnimData(graphic);
	mis._mimfnum = static_cast<int32_t>(midir);
	mis._miAnimFlags = MissileGraphicsFlags::None;
	ClxSpriteList sprites = *anim.spritesForDirection(midir);
	const uint16_t width = sprites[0].width();
	mis._miAnimData.emplace(sprites);
	mis._miAnimDelay = anim.rate;
	mis._miAnimLen = anim.frames;
	mis._miAnimWidth = width;
	mis._miAnimWidth2 = CalculateWidth2(width);
	mis._miAnimAdd = 1;
	mis.var1 = 0;
	mis.var2 = 0;
	mis._miLightFlag = true;
	mis._mirange = 256;
}
} // namespace

void AddRhino(Missile &missile, AddMissileParameter &parameter)
{
	Monster &monster = Monsters[missile._misource];

	MonsterGraphic graphic = MonsterGraphic::Walk;
	if (IsAnyOf(monster.type().type, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
		graphic = MonsterGraphic::Special;
	} else if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
		graphic = MonsterGraphic::Attack;
	}
	UpdateMissileVelocity(missile, parameter.dst, 18);
	InitMissileAnimationFromMonster(missile, parameter.midir, monster, graphic);
	if (IsAnyOf(monster.type().type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE))
		missile._miAnimFrame = 7;
	if (monster.isUnique()) {
		missile._mlid = monster.lightId;
	}
	PutMissile(missile);
}

void AddGenericMagicMissile(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 16);
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	if (missile._micaster != TARGET_MONSTERS && missile._misource > 0) {
		auto &monster = Monsters[missile._misource];
		if (monster.type().type == MT_SUCCUBUS)
			SetMissAnim(missile, MissileGraphicID::BloodStar);
		if (monster.type().type == MT_SNOWWICH)
			SetMissAnim(missile, MissileGraphicID::BloodStarBlue);
		if (monster.type().type == MT_HLSPWN)
			SetMissAnim(missile, MissileGraphicID::BloodStarRed);
		if (monster.type().type == MT_SOLBRNR)
			SetMissAnim(missile, MissileGraphicID::BloodStarYellow);
	}

	if (GetMissileSpriteData(missile._miAnimType).animFAmt == 16) {
		SetMissDir(missile, GetDirection16(missile.position.start, dst));
	}

	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			const Player &player = *missile.sourcePlayer();
			int dmgBase = (missile._mispllvl * 3) + 4;
			int minDmg = (ScaleSpellEffect(dmgBase, missile._mispllvl)) / 2;
			int maxDmg = (ScaleSpellEffect(dmgBase + 36, missile._mispllvl)) / 2;
			missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;
			if (player._pIMisType == 5) {
				if (!(player.executedSpell.spellType == SpellType::Spell
				        && player.executedSpell.spellType == SpellType::Scroll
				        && player.executedSpell.spellType == SpellType::Charges)) {
					missile._midam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
				}
			}
			break;
		}
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
}

void AddAcid(Missile &missile, AddMissileParameter &parameter)
{
	UpdateMissileVelocity(missile, parameter.dst, 16);
	SetMissDir(missile, GetDirection16(missile.position.start, parameter.dst));
	if (!gbIsHellfire || (missile.position.velocity.deltaX & 0xFFFF0000) != 0 || (missile.position.velocity.deltaY & 0xFFFF0000) != 0
	    || missile.sourceType() != MissileSource::Player) {
		missile._mirange = 5 * (Monsters[missile._misource].intelligence + 4);
	} else {
		missile._mirange = 1;
	}
	missile._mlid = NO_LIGHT;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	if (missile._midam == 0) {
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			Player &player = Players[missile._misource];
			missile._midam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
		} break;
		case MissileSource::Monster:
			missile._midam = ProjectileMonsterDamage(missile);
			break;
		case MissileSource::Trap:
			missile._midam = ProjectileTrapDamage(missile);
			break;
		}
	}
	PutMissile(missile);
}

void AddAcidPuddle(Missile &missile, AddMissileParameter & /*parameter*/)
{
	switch (missile.sourceType()) {
	case MissileSource::Player: {
		Player &player = Players[missile._misource];
		missile._miLightFlag = true;
		missile._mirange = GenerateRnd((player._pLevel * 2) + 1) + GenerateRnd(std::min(player._pIMMaxDam, 200) + 1);
		missile._miPreFlag = true;
	} break;
	case MissileSource::Monster: {
		missile._miLightFlag = true;
		int monst = missile._misource;
		missile._mirange = GenerateRnd(15) + 40 * (Monsters[monst].intelligence + 1);
		missile._miPreFlag = true;
	} break;
	}
}

void AddStoneCurse(Missile &missile, AddMissileParameter &parameter)
{
	std::optional<Point> targetMonsterPosition = FindClosestValidPosition(
	    [](Point target) {
		    if (!InDungeonBounds(target)) {
			    return false;
		    }

		    int monsterId = abs(dMonster[target.x][target.y]) - 1;
		    if (monsterId < 0) {
			    return false;
		    }

		    auto &monster = Monsters[monsterId];

		    if (IsAnyOf(monster.type().type, MT_GOLEM, MT_DIABLO, MT_NAKRUL)) {
			    return false;
		    }
		    if (IsAnyOf(monster.mode, MonsterMode::FadeIn, MonsterMode::FadeOut, MonsterMode::Charge)) {
			    return false;
		    }

		    return true;
	    },
	    parameter.dst, 0, 5);

	if (!targetMonsterPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	// Petrify the targeted monster
	int monsterId = abs(dMonster[targetMonsterPosition->x][targetMonsterPosition->y]) - 1;
	auto &monster = Monsters[monsterId];

	if (monster.mode == MonsterMode::Petrified) {
		// Monster is already petrified and StoneCurse doesn't stack
		missile._miDelFlag = true;
		return;
	}

	missile.var1 = static_cast<int>(monster.mode);
	missile.var2 = monsterId;
	monster.petrify();

	// And set up the missile to unpetrify it in the future
	missile.position.tile = *targetMonsterPosition;
	missile.position.start = missile.position.tile;
	missile._mirange = missile._mispllvl + 6;

	if (missile._mirange > 15)
		missile._mirange = 15;
	missile._mirange <<= 4;
}

void AddGolem(Missile &missile, AddMissileParameter &parameter)
{
	missile._miDelFlag = true;

	int playerId = missile._misource;
	Player &player = Players[playerId];
	Monster &golem = Monsters[playerId];

	if (golem.position.tile != GolemHoldingCell && &player == MyPlayer)
		KillMyGolem();

	if (golem.position.tile == GolemHoldingCell) {
		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [start = missile.position.start](Point target) {
			    return !IsTileOccupied(target) && LineClearMissile(start, target);
		    },
		    parameter.dst, 0, 5);

		if (spawnPosition) {
			SpawnGolem(player, golem, *spawnPosition, missile);
		}
	}
}

void AddApocalypseBoom(Missile &missile, AddMissileParameter &parameter)
{
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._mirange = missile._miAnimLen;
}

void AddHealing(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	int hp = GenerateRnd(10) + 1;
	hp += GenerateRndSum(4, player._pLevel) + player._pLevel;
	hp += GenerateRndSum(6, missile._mispllvl) + missile._mispllvl;
	hp <<= 6;

	if (player._pClass == HeroClass::Warrior || player._pClass == HeroClass::Barbarian || player._pClass == HeroClass::Monk) {
		hp *= 2;
	} else if (player._pClass == HeroClass::Rogue || player._pClass == HeroClass::Bard) {
		hp += hp / 2;
	}

	player._pHitPoints = std::min(player._pHitPoints + hp, player._pMaxHP);
	player._pHPBase = std::min(player._pHPBase + hp, player._pMaxHPBase);

	missile._miDelFlag = true;
	RedrawComponent(PanelDrawComponent::Health);
}

void AddHealOther(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		NewCursor(CURSOR_HEALOTHER);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
}

void AddElemental(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}

	Player &player = Players[missile._misource];

	int dmg = 2 * (player._pLevel + GenerateRndSum(10, 2)) + 4;
	missile._midam = ScaleSpellEffect(dmg, missile._mispllvl) / 2;

	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile._mlid = AddLight(missile.position.start, 8);
}

extern void FocusOnInventory();

void AddIdentify(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_IDENTIFY);
	}
}

void AddFireWallControl(Missile &missile, AddMissileParameter &parameter)
{
	std::optional<Point> spreadPosition = FindClosestValidPosition(
	    [start = missile.position.start](Point target) {
		    return start != target && IsTileNotSolid(target) && !IsObjectAtPosition(target) && LineClearMissile(start, target);
	    },
	    parameter.dst, 0, 5);

	if (!spreadPosition) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	missile._miDelFlag = false;
	missile.var1 = spreadPosition->x;
	missile.var2 = spreadPosition->y;
	missile.var5 = spreadPosition->x;
	missile.var6 = spreadPosition->y;
	missile.var3 = static_cast<int>(Left(Left(parameter.midir)));
	missile.var4 = static_cast<int>(Right(Right(parameter.midir)));
	missile._mirange = 7;
}

void AddInfravision(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._mirange = ScaleSpellEffect(1584, missile._mispllvl);
}

void AddFlameWaveControl(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;
	missile._mirange = 1;
	missile._miAnimFrame = 4;
}

void AddNova(Missile &missile, AddMissileParameter &parameter)
{
	missile.var1 = parameter.dst.x;
	missile.var2 = parameter.dst.y;

	if (!missile.IsTrap()) {
		Player &player = Players[missile._misource];
		int minDmg = (ScaleSpellEffect((player._pLevel + 5) / 2, missile._mispllvl) * 5) / 6;
		int maxDmg = (ScaleSpellEffect((player._pLevel + 30) / 2, missile._mispllvl) * 5) / 6;
		missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;
	} else {
		missile._midam = (currlevel / 2) + GenerateRndSum(3, 3);
	}

	missile._mirange = 1;
}

void AddRage(Missile &missile, AddMissileParameter &parameter)
{
	Player &player = Players[missile._misource];

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive | SpellFlag::RageCooldown) || player._pHitPoints <= player._pLevel << 6) {
		missile._miDelFlag = true;
		parameter.spellFizzled = true;
		return;
	}

	int tmp = 3 * player._pLevel;
	tmp <<= 7;
	player._pSpellFlags |= SpellFlag::RageActive;
	missile.var2 = tmp;
	int lvl = player._pLevel * 2;
	missile._mirange = lvl + 10 * missile._mispllvl + 245;
	CalcPlrItemVals(player, true);
	player._pMana = player._pMana / 2;
	RedrawEverything();
	player.Say(HeroSpeech::Aaaaargh);
}

void AddItemRepair(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_REPAIR);
	}
}

void AddStaffRecharge(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		if (sbookflag)
			sbookflag = false;
		if (!invflag) {
			invflag = true;
			if (ControlMode != ControlTypes::KeyboardAndMouse)
				FocusOnInventory();
		}
		NewCursor(CURSOR_RECHARGE);
	}
}

void AddTrapDisarm(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer) {
		NewCursor(CURSOR_DISARM);
		if (ControlMode != ControlTypes::KeyboardAndMouse) {
			if (ObjectUnderCursor != nullptr) {
				NetSendCmdLoc(MyPlayerId, true, CMD_DISARMXY, cursPosition);
				if (sgGameInitInfo.nDifficulty == DIFF_NORMAL) {
					player._pExperience += 100;
				} else if (sgGameInitInfo.nDifficulty == DIFF_NIGHTMARE) {
					player._pExperience += 500;
				} else if (sgGameInitInfo.nDifficulty == DIFF_HELL) {
					player._pExperience += 1000;
				}
			} else
				NewCursor(CURSOR_HAND);
		}
	}
}

void AddApocalypse(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile.var1 = 8;
	missile.var2 = std::max(missile.position.start.y - 8, 1);
	missile.var3 = std::min(missile.position.start.y + 8, MAXDUNY - 1);
	missile.var4 = std::max(missile.position.start.x - 8, 1);
	missile.var5 = std::min(missile.position.start.x + 8, MAXDUNX - 1);
	missile.var6 = missile.var4;

	int minDmg = player._pLevel + (missile._mispllvl * 5);
	int maxDmg = (player._pLevel * 2) + (missile._mispllvl * 10) + std::min(150, player._pMagic);

	missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;

	missile._mirange = 255;
}

void AddInferno(Missile &missile, AddMissileParameter &parameter)
{
	constexpr int equippedInferno = 4;
	constexpr int equippedInfernoThunderclap = 7;
	constexpr int equippedInfernos = 8;
	constexpr int equippedInfernoTsangs = 104;

	auto IsEquippedInfernoType = [](int misType) {
		return misType == equippedInferno || misType == equippedInfernoThunderclap || misType == equippedInfernos || misType == equippedInfernoTsangs;
	};

	auto IsStandardSpellType = [](SpellType type) {
		return type == SpellType::Spell || type == SpellType::Scroll || type == SpellType::Charges;
	};

	missile.var2 = 5 * missile._midam;
	missile.position.start = parameter.dst;

	SyncPositionWithParent(missile, parameter);

	missile._mirange = missile.var2 + 20;
	missile._mlid = AddLight(missile.position.start, 1);

	Player &player = Players[missile._misource];

	if (missile._micaster == TARGET_MONSTERS) {
		if (IsEquippedInfernoType(player._pIMisType) && !IsStandardSpellType(player.executedSpell.spellType)) {
			missile._midam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam);
		} else {
			int mind = missile._mispllvl + player._pLevel / 2;
			int maxd = (missile._mispllvl * 2) + player._pLevel + player._pMagic / 2;
			missile._midam = mind + GenerateRnd(maxd - mind + 1);
		}
	} else {
		auto &monster = Monsters[missile._misource];
		missile._midam = monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1);
	}
}

void AddInfernoControl(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == parameter.dst) {
		dst += parameter.midir;
	}
	UpdateMissileVelocity(missile, dst, 32);
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mirange = 256;
}

void AddChargedBolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	Player &player = Players[missile._misource];
	missile._mirnd = GenerateRnd(15) + 1;
	missile._midam = (missile._micaster == TARGET_MONSTERS) ? (GenerateRnd(Players[missile._misource]._pMagic / 4 + 1) + player._pLevel + 1) : 15;

	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	missile._miAnimFrame = GenerateRnd(8) + 1;
	missile._mlid = AddLight(missile.position.start, 5);

	UpdateMissileVelocity(missile, dst, 8);
	missile.var1 = 5;
	missile.var2 = static_cast<int>(parameter.midir);
	missile._mirange = 256;
}

void AddHolyBolt(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}
	int sp = 16;
	if (!missile.IsTrap()) {
		sp += std::min(missile._mispllvl * 2, 47);
	}

	Player &player = Players[missile._misource];
	UpdateMissileVelocity(missile, dst, sp);
	SetMissDir(missile, GetDirection16(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile._mlid = AddLight(missile.position.start, 8);
	int minDmg = player._pLevel + 9;
	int maxDmg = minDmg + player._pLevel + 9;
	missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;
	if (player._pIMisType == 100 || player._pIMisType == 103
	    || player._pIMisType == 104 || player._pIMisType == 200) {
		if (!(player.executedSpell.spellType == SpellType::Spell
		        && player.executedSpell.spellType == SpellType::Scroll
		        && player.executedSpell.spellType == SpellType::Charges)) {
			missile._midam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
		}
	}
}

void AddResurrect(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	if (&player == MyPlayer) {
		NewCursor(CURSOR_RESURRECT);
		if (ControlMode != ControlTypes::KeyboardAndMouse)
			TryIconCurs();
	}
	missile._miDelFlag = true;
}

void AddResurrectBeam(Missile &missile, AddMissileParameter &parameter)
{
	missile.position.tile = parameter.dst;
	missile.position.start = parameter.dst;
	missile._mirange = GetMissileSpriteData(MissileGraphicID::Resurrect).animLen(0);
}

void AddTelekinesis(Missile &missile, AddMissileParameter & /*parameter*/)
{
	Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	if (&player == MyPlayer)
		NewCursor(CURSOR_TELEKINESIS);
}

void AddBoneSpirit(Missile &missile, AddMissileParameter &parameter)
{
	Point dst = parameter.dst;
	if (missile.position.start == dst) {
		dst += parameter.midir;
	}

	Player &player = Players[missile._misource];
	auto &BoneArmor = player.InvBody[INVLOC_CHEST];
	UpdateMissileVelocity(missile, dst, 16);
	SetMissDir(missile, GetDirection(missile.position.start, dst));
	missile._mirange = 256;
	missile.var1 = missile.position.start.x;
	missile.var2 = missile.position.start.y;
	missile.var3 = 0;
	missile.var4 = dst.x;
	missile.var5 = dst.y;
	missile.var6 = -1; // Initialize to -1 to not ignore any monster initially
	missile.var7 = -1; // Initialize to -1 to not ignore any monster initially
	if (player._pIMisType == 9
	    || HasAllOf(BoneArmor._iFlags, ItemSpecialEffect::MagicDamage | ItemSpecialEffect::FastestHitRecovery) && HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
		missile.var7 = 1;
	}
	missile._mlid = AddLight(missile.position.start, 8);
}

void AddRedPortal(Missile &missile, AddMissileParameter & /*parameter*/)
{
	missile._mirange = 100;
	missile.var1 = 100 - missile._miAnimLen;
	PutMissile(missile);
}

void AddDiabloApocalypse(Missile &missile, AddMissileParameter & /*parameter*/)
{
	for (const Player &player : Players) {
		if (!player.plractive)
			continue;
		if (!LineClearMissile(missile.position.start, player.position.future))
			continue;

		AddMissile({ 0, 0 }, player.position.future, Direction::South, MissileID::DiabloApocalypseBoom, missile._micaster, missile._misource, missile._midam, 0);
	}
	missile._miDelFlag = true;
}

Missile *AddMissile(Point src, Point dst, Direction midir, MissileID mitype,
    mienemy_type micaster, int id, int midam, int spllvl,
    Missile *parent, std::optional<_sfx_id> lSFX)
{
	if (Missiles.size() >= Missiles.max_size()) {
		return nullptr;
	}

	Missiles.emplace_back(Missile {});
	auto &missile = Missiles.back();

	const MissileData &missileData = GetMissileData(mitype);

	missile._mitype = mitype;
	missile._micaster = micaster;
	missile._misource = id;
	missile._midam = midam;
	missile._mispllvl = spllvl;
	missile.position.tile = src;
	missile.position.start = src;
	missile._miAnimAdd = 1;
	missile._miAnimType = missileData.mFileNum;
	missile._miDrawFlag = missileData.isDrawn();
	missile._mlid = NO_LIGHT;
	missile.lastCollisionTargetHash = 0;

	if (!missile.IsTrap() && micaster == TARGET_PLAYERS) {
		Monster &monster = Monsters[id];
		if (monster.isUnique()) {
			missile._miUniqTrans = monster.uniqTrans + 1;
		}
	}

	if (missile._miAnimType == MissileGraphicID::None || GetMissileSpriteData(missile._miAnimType).animFAmt < 8)
		SetMissDir(missile, 0);
	else
		SetMissDir(missile, midir);

	if (!lSFX) {
		lSFX = missileData.mlSFX;
	}

	if (*lSFX != SFX_NONE) {
		PlaySfxLoc(*lSFX, missile.position.start);
	}

	AddMissileParameter parameter = { dst, midir, parent, false };
	missileData.mAddProc(missile, parameter);
	if (parameter.spellFizzled) {
		return nullptr;
	}

	return &missile;
}

void ProcessElementalArrow(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType == MissileGraphicID::ChargedBolt || missile._miAnimType == MissileGraphicID::MagmaBallExplosion) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 5);
	} else {
		int mind;
		int maxd;
		int p = missile._misource;
		missile._midist++;
		if (!missile.IsTrap()) {
			if (missile._micaster == TARGET_MONSTERS) {
				// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
				const Player &player = Players[p];
				mind = player._pIMinDam;
				maxd = player._pIMaxDam;
			} else {
				// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
				Monster &monster = Monsters[p];
				mind = monster.minDamage;
				maxd = monster.maxDamage;
			}
		} else {
			mind = GenerateRnd(10) + 1 + currlevel;
			maxd = GenerateRnd(10) + 1 + currlevel * 2;
		}
		MoveMissileAndCheckMissileCol(missile, DamageType::Physical, mind, maxd, true, false);
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();

			int eMind;
			int eMaxd;
			MissileGraphicID eAnim;
			DamageType damageType;
			switch (missile._mitype) {
			case MissileID::LightningArrow:
				if (!missile.IsTrap()) {
					// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
					const Player &player = Players[p];
					eMind = player._pILMinDam;
					eMaxd = player._pILMaxDam;
				} else {
					eMind = GenerateRnd(10) + 1 + currlevel;
					eMaxd = GenerateRnd(10) + 1 + currlevel * 2;
				}
				eAnim = MissileGraphicID::ChargedBolt;
				damageType = DamageType::Lightning;
				break;
			case MissileID::FireArrow:
				if (!missile.IsTrap()) {
					// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
					const Player &player = Players[p];
					eMind = player._pIFMinDam;
					eMaxd = player._pIFMaxDam;
				} else {
					eMind = GenerateRnd(10) + 1 + currlevel;
					eMaxd = GenerateRnd(10) + 1 + currlevel * 2;
				}
				eAnim = MissileGraphicID::MagmaBallExplosion;
				damageType = DamageType::Fire;
				break;
			default:
				app_fatal(StrCat("wrong missile ID ", static_cast<int>(missile._mitype)));
				break;
			}
			SetMissAnim(missile, eAnim);
			CheckMissileCol(missile, damageType, eMind, eMaxd, false, missile.position.tile, true);
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 5);
			}
		}
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessArrow(Missile &missile)
{
	missile._mirange--;
	missile._midist++;

	int mind;
	int maxd;
	switch (missile.sourceType()) {
	case MissileSource::Player: {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		const Player &player = *missile.sourcePlayer();
		mind = player._pIMinDam;
		maxd = player._pIMaxDam;
	} break;
	case MissileSource::Monster: {
		// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
		const Monster &monster = *missile.sourceMonster();
		mind = monster.minDamage;
		maxd = monster.maxDamage;
	} break;
	case MissileSource::Trap:
		mind = currlevel;
		maxd = 2 * currlevel;
		break;
	}
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), mind, maxd, true, false);
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessGenericProjectile(Missile &missile)
{
	missile._mirange--;

	int minDam = missile._midam;
	int maxDam = missile._midam;
	if (missile._micaster != TARGET_MONSTERS) {
		auto &monster = Monsters[missile._misource];
		minDam = monster.minDamage;
		maxDam = monster.maxDamage;
	}

	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), minDam, maxDam, true, true);
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		Point dst = { 0, 0 };
		auto dir = static_cast<Direction>(missile._mimfnum);
		switch (missile._mitype) {
		case MissileID::Firebolt:
		case MissileID::MagmaBall:
			AddMissile(missile.position.tile, dst, dir, MissileID::MagmaBallExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BloodStar:
			AddMissile(missile.position.tile, dst, dir, MissileID::BloodStarExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::Acid:
			AddMissile(missile.position.tile, dst, dir, MissileID::AcidSplat, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::OrangeFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::OrangeExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BlueFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::BlueExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::RedFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::RedExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::YellowFlare:
			AddMissile(missile.position.tile, dst, dir, MissileID::YellowExplosion, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		case MissileID::BlueFlare2:
			AddMissile(missile.position.tile, dst, dir, MissileID::BlueExplosion2, missile._micaster, missile._misource, 0, 0, &missile);
			break;
		default:
			break;
		}
		if (missile._mlid != NO_LIGHT)
			AddUnLight(missile._mlid);
		PutMissile(missile);
	} else {
		if (missile.position.tile != Point { missile.var1, missile.var2 }) {
			missile.var1 = missile.position.tile.x;
			missile.var2 = missile.position.tile.y;
			if (missile._mlid != NO_LIGHT)
				ChangeLight(missile._mlid, missile.position.tile, 8);
		}
		PutMissile(missile);
	}
}

void ProcessNovaBall(Missile &missile)
{
	Point targetPosition = { missile.var1, missile.var2 };
	missile._mirange--;
	int j = missile._mirange;
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._mirange = j;

	if (missile.position.tile == targetPosition) {
		Object *object = FindObjectAtPosition(targetPosition);
		if (object != nullptr && object->IsShrine()) {
			missile._mirange = j;
		}
	}
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessAcidPuddle(Missile &missile)
{
	missile._mirange--;
	int range = missile._mirange;
	CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	missile._mirange = range;
	if (range == 0) {
		if (missile._mimfnum != 0) {
			missile._miDelFlag = true;
		} else {
			SetMissDir(missile, 1);
			missile._mirange = missile._miAnimLen;
		}
	}
	PutMissile(missile);
}

void ProcessFireWall(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	missile._mirange--;
	if (missile._mirange == missile.var1) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	if (missile._mirange == missile._miAnimLen - 1) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 13;
		missile._miAnimAdd = -1;
	}
	CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, true);
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 && missile._mirange != 0 && missile._miAnimAdd != -1 && missile.var2 < 12) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	PutMissile(missile);
}

void ProcessFireball(Missile &missile)
{
	missile._mirange--;

	if (missile._miAnimType == MissileGraphicID::BigExplosion) {
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		int minDam = missile._midam;
		int maxDam = missile._midam;

		if (missile._micaster != TARGET_MONSTERS) {
			auto &monster = Monsters[missile._misource];
			minDam = monster.minDamage;
			maxDam = monster.maxDamage;
		}
		const DamageType damageType = GetMissileData(missile._mitype).damageType();
		MoveMissileAndCheckMissileCol(missile, damageType, minDam, maxDam, true, false);
		if (missile._mirange == 0) {
			const Point missilePosition = missile.position.tile;
			ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

			constexpr Direction Offsets[] = {
				Direction::NoDirection,
				Direction::SouthWest,
				Direction::NorthEast,
				Direction::SouthEast,
				Direction::East,
				Direction::South,
				Direction::NorthWest,
				Direction::West,
				Direction::North
			};
			for (Direction offset : Offsets) {
				if (!CheckBlock(missile.position.start, missilePosition + offset))
					CheckMissileCol(missile, damageType, minDam, maxDam, false, missilePosition + offset, true);
			}

			if (!TransList[dTransVal[missilePosition.x][missilePosition.y]]
			    || (missile.position.velocity.deltaX < 0 && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y + 1], TileProperties::Solid)) || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y - 1], TileProperties::Solid))))) {
				missile.position.tile += Displacement { 1, 1 };
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaY > 0
			    && ((TransList[dTransVal[missilePosition.x + 1][missilePosition.y]] && TileHasAny(dPiece[missilePosition.x + 1][missilePosition.y], TileProperties::Solid))
			        || (TransList[dTransVal[missilePosition.x - 1][missilePosition.y]] && TileHasAny(dPiece[missilePosition.x - 1][missilePosition.y], TileProperties::Solid)))) {
				missile.position.offset.deltaY -= 32;
			}
			if (missile.position.velocity.deltaX > 0
			    && ((TransList[dTransVal[missilePosition.x][missilePosition.y + 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y + 1], TileProperties::Solid))
			        || (TransList[dTransVal[missilePosition.x][missilePosition.y - 1]] && TileHasAny(dPiece[missilePosition.x][missilePosition.y - 1], TileProperties::Solid)))) {
				missile.position.offset.deltaX -= 32;
			}
			missile._mimfnum = 0;
			SetMissAnim(missile, MissileGraphicID::BigExplosion);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.velocity = {};
		} else if (missile.position.tile != Point { missile.var1, missile.var2 }) {
			missile.var1 = missile.position.tile.x;
			missile.var2 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	}

	PutMissile(missile);
}

void ProcessHorkSpawn(Missile &missile)
{
	missile._mirange--;
	CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), 0, 0, false, missile.position.tile, false);
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;

		std::optional<Point> spawnPosition = FindClosestValidPosition(
		    [](Point target) {
			    return !IsTileOccupied(target);
		    },
		    missile.position.tile, 0, 1);

		if (spawnPosition) {
			auto facing = static_cast<Direction>(missile.var1);
			Monster *monster = AddMonster(*spawnPosition, facing, 1, true);
			if (monster != nullptr) {
				M_StartStand(*monster, facing);
			}
		}
	} else {
		missile._midist++;
		missile.position.traveled += missile.position.velocity;
		UpdateMissilePos(missile);
	}
	PutMissile(missile);
}

void ProcessRune(Missile &missile)
{
	Point position = missile.position.tile;
	int mid = dMonster[position.x][position.y];
	int pid = dPlayer[position.x][position.y];
	if (mid != 0 || pid != 0) {
		Point targetPosition = mid != 0 ? Monsters[abs(mid) - 1].position.tile : Players[abs(pid) - 1].position.tile;
		Direction dir = GetDirection(position, targetPosition);

		missile._miDelFlag = true;
		AddUnLight(missile._mlid);

		AddMissile(position, position, dir, static_cast<MissileID>(missile.var1), TARGET_BOTH, missile._misource, missile._midam, missile._mispllvl);
	}

	PutMissile(missile);
}

void ProcessLightningWall(Missile &missile)
{
	missile._mirange--;
	int range = missile._mirange;
	CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag)
		missile._mirange = range;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessBigExplosion(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessLightningBow(Missile &missile)
{
	SpawnLightning(missile, missile._midam);
}

void ProcessRingOfFire(Missile &missile)
{
	missile._miDelFlag = true;
	int8_t src = missile._misource;
	uint8_t lvl = missile._micaster == TARGET_MONSTERS ? Players[src]._pLevel : currlevel;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2) / 2;

	if (missile.limitReached)
		return;

	Crawl(3, [&](Displacement displacement) {
		Point target = Point { missile.var1, missile.var2 } + displacement;
		if (!InDungeonBounds(target))
			return false;
		int dp = dPiece[target.x][target.y];
		if (TileHasAny(dp, TileProperties::Solid))
			return false;
		if (IsObjectAtPosition(target))
			return false;
		if (!LineClearMissile(missile.position.tile, target))
			return false;
		if (TileHasAny(dp, TileProperties::BlockMissile)) {
			missile.limitReached = true;
			return true;
		}

		AddMissile(target, target, Direction::South, MissileID::FireWall, TARGET_BOTH, src, dmg, missile._mispllvl);
		return false;
	});
}

void ProcessSearch(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange != 0)
		return;

	const Player &player = Players[missile._misource];

	missile._miDelFlag = true;
	PlaySfxLoc(IS_CAST7, player.position.tile);
	if (&player == MyPlayer)
		AutoMapShowItems = false;
}

void ProcessLightningWallControl(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;
	int lvl = !missile.IsTrap() ? Players[id]._pLevel : 0;
	int dmg = 16 * (GenerateRndSum(10, 2) + lvl + 2);

	{
		Point position = { missile.var1, missile.var2 };
		Point target = position + static_cast<Direction>(missile.var3);

		if (!missile.limitReached && GrowWall(id, position, target, MissileID::LightningWall, missile._mispllvl, dmg)) {
			missile.var1 = target.x;
			missile.var2 = target.y;
		} else {
			missile.limitReached = true;
		}
	}

	{
		Point position = { missile.var5, missile.var6 };
		Point target = position + static_cast<Direction>(missile.var4);

		if (missile.var7 == 0 && GrowWall(id, position, target, MissileID::LightningWall, missile._mispllvl, dmg)) {
			missile.var5 = target.x;
			missile.var6 = target.y;
		} else {
			missile.var7 = 1;
		}
	}
}

void ProcessNovaCommon(Missile &missile, MissileID projectileType)
{
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Direction dir = Direction::South;
	mienemy_type en = TARGET_PLAYERS;
	if (!missile.IsTrap()) {
		dir = Players[id]._pdir;
		en = TARGET_MONSTERS;
	}

	constexpr std::array<WorldTileDisplacement, 9> quarterRadius = { { { 4, 0 }, { 4, 1 }, { 4, 2 }, { 4, 3 }, { 4, 4 }, { 3, 4 }, { 2, 4 }, { 1, 4 }, { 0, 4 } } };
	for (WorldTileDisplacement quarterOffset : quarterRadius) {
		// This ends up with two missiles targeting offsets 4,0, 0,4, -4,0, 0,-4.
		std::array<WorldTileDisplacement, 4> offsets { quarterOffset, quarterOffset.flipXY(), quarterOffset.flipX(), quarterOffset.flipY() };
		for (WorldTileDisplacement offset : offsets)
			AddMissile(src, src + offset, dir, projectileType, en, id, dam, missile._mispllvl);
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void ProcessImmolation(Missile &missile)
{
	ProcessNovaCommon(missile, MissileID::FireballBow);
}

void ProcessNova(Missile &missile)
{
	ProcessNovaCommon(missile, MissileID::NovaBall);
}

Direction OppositeIMisDir(Direction dir)
{
	if (dir == Direction::South) {
		dir = Direction::North;
	} else if (dir == Direction::North) {
		dir = Direction::South;
	} else if (dir == Direction::East) {
		dir = Direction::West;
	} else if (dir == Direction::West) {
		dir = Direction::East;
	} else if (dir == Direction::NorthEast) {
		dir = Direction::SouthWest;
	} else if (dir == Direction::NorthWest) {
		dir = Direction::SouthEast;
	} else if (dir == Direction::SouthWest) {
		dir = Direction::NorthEast;
	} else if (dir == Direction::SouthEast) {
		dir = Direction::NorthWest;
	}

	return dir;
}

std::pair<Direction, Direction> NextIMisDir(Direction dir)
{
	switch (dir) {
	case Direction::North:
		return { Direction::NorthEast, Direction::NorthWest };
	case Direction::NorthEast:
		return { Direction::North, Direction::East };
	case Direction::East:
		return { Direction::NorthEast, Direction::SouthEast };
	case Direction::SouthEast:
		return { Direction::East, Direction::South };
	case Direction::South:
		return { Direction::SouthEast, Direction::SouthWest };
	case Direction::SouthWest:
		return { Direction::South, Direction::West };
	case Direction::West:
		return { Direction::SouthWest, Direction::NorthWest };
	case Direction::NorthWest:
		return { Direction::West, Direction::North };
	default:
		return { Direction::SouthEast, Direction::SouthWest };
	}
}

void ProcessSpectralArrow(Missile &missile)
{
	int id = missile._misource;
	int dam = missile._midam;
	Point src = missile.position.tile;
	Point dst = { missile.var1, missile.var2 };
	int spllvl = missile.var3;
	MissileID mitype = MissileID::Arrow;
	Direction dir = Direction::South;
	mienemy_type micaster = TARGET_PLAYERS;

	if (!missile.IsTrap()) {
		const Player &player = Players[id];
		dir = player._pdir;
		micaster = TARGET_MONSTERS;

		switch (player._pIMisType) {
		case 1:
			mitype = MissileID::FireballBow;
			dam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam + 1);
			AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			break;
		case 2:
			mitype = MissileID::LightningBow;
			dam = ((player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1)) / 6);
			AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			break;
		case 3:
		case 6:
			mitype = MissileID::ChargedBoltBow;
			if (player._pIMisType == 3 && !HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
				dam = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
				std::pair<Direction, Direction> novadir = NextIMisDir(dir);
				AddMissile(src, dst, novadir.first, mitype, micaster, id, dam, spllvl);
				AddMissile(src, dst, novadir.second, mitype, micaster, id, dam, spllvl);
				dir = OppositeIMisDir(dir);
				AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			} else if (player._pIMisType == 6 || player._pIMisType == 3 && HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
				dam = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
				std::pair<Direction, Direction> novadir = NextIMisDir(dir);
				AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
				AddMissile(src, dst, novadir.first, mitype, micaster, id, dam, spllvl);
				AddMissile(src, dst, novadir.second, mitype, micaster, id, dam, spllvl);
				dir = OppositeIMisDir(dir);
				std::pair<Direction, Direction> novadir2 = NextIMisDir(dir);
				AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
				AddMissile(src, dst, novadir2.first, mitype, micaster, id, dam, spllvl);
				AddMissile(src, dst, novadir2.second, mitype, micaster, id, dam, spllvl);
			}
			break;
		case 4:
		case 7:
		case 8:
			mitype = MissileID::InfernoControl;
			if (player._pIMisType == 4 && !HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
				dam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam + 1);
				std::pair<Direction, Direction> infernodir = NextIMisDir(dir);
				NetSendAddMissile(missile.position.tile, missile.position.start, dir, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.first, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.second, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
			} else if (player._pIMisType == 8 || player._pIMisType == 4 && HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
				dam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam + 1);
				std::pair<Direction, Direction> infernodir = NextIMisDir(dir);
				NetSendAddMissile(missile.position.tile, missile.position.start, dir, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.first, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.second, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				dir = OppositeIMisDir(dir);
				std::pair<Direction, Direction> infernodir2 = NextIMisDir(dir);
				NetSendAddMissile(missile.position.tile, missile.position.start, dir, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir2.first, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
				NetSendAddMissile(missile.position.tile, missile.position.start, infernodir2.second, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
			} else if (player._pIMisType == 7) {
				if (mitype == MissileID::InfernoControl) {
					dam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam + 1);
					std::pair<Direction, Direction> infernodir = NextIMisDir(dir);
					NetSendAddMissile(missile.position.tile, missile.position.start, dir, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
					NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.first, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
					NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.second, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), id, dam, spllvl);
					mitype = MissileID::ChargedBoltBow;
				}
				if (mitype == MissileID::ChargedBoltBow) {
					dam = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
					std::pair<Direction, Direction> novadir = NextIMisDir(dir);
					AddMissile(src, dst, novadir.first, mitype, micaster, id, dam, spllvl);
					AddMissile(src, dst, novadir.second, mitype, micaster, id, dam, spllvl);
					dir = OppositeIMisDir(dir);
					AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
				}
			}
			break;
		case 5:
			mitype = MissileID::BloodStar;
			dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
			AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			break;
		case 9:
			mitype = MissileID::BoneSpirit;
			dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
			AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			break;
		case 10:
			mitype = MissileID::Acid;
			dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
			AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			break;
		case 100:
		case 103:
		case 104:
		case 200:
			mitype = MissileID::HolyBoltBow;
			if (player._pIMisType == 103) {
				if (mitype == MissileID::HolyBoltBow) {
					dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
					AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
					mitype = MissileID::ChargedBoltBow;
				}
				if (mitype == MissileID::ChargedBoltBow) {
					dam = player._pILMinDam + GenerateRnd(player._pILMaxDam - player._pILMinDam + 1);
					std::pair<Direction, Direction> novadir = NextIMisDir(player._pdir);
					AddMissile(player.position.tile, player.position.temp, novadir.first, mitype, TARGET_MONSTERS, player.getId(), dam, spllvl);
					AddMissile(player.position.tile, player.position.temp, novadir.second, mitype, TARGET_MONSTERS, player.getId(), dam, spllvl);
					AddMissile(player.position.tile, player.position.temp, dir, mitype, TARGET_MONSTERS, player.getId(), dam, spllvl);
				}
			} else if (player._pIMisType == 104) {
				if (mitype == MissileID::HolyBoltBow) {
					dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
					AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
					mitype = MissileID::InfernoControl;
				}
				if (mitype == MissileID::InfernoControl) {
					dam = player._pIFMinDam + GenerateRnd(player._pIFMaxDam - player._pIFMinDam + 1);
					std::pair<Direction, Direction> infernodir = NextIMisDir(player._pdir);
					NetSendAddMissile(missile.position.tile, missile.position.start, player._pdir, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), dam, spllvl);
					NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.first, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), dam, spllvl);
					NetSendAddMissile(missile.position.tile, missile.position.start, infernodir.second, static_cast<uint16_t>(mitype), static_cast<uint16_t>(TARGET_MONSTERS), player.getId(), dam, spllvl);
				}
			} else if (player._pIMisType == 100) {
				dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
				AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			} else if (player._pIMisType == 200) {
				dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
				dam += dam;
				AddMissile(src, dst, dir, mitype, micaster, id, dam, spllvl);
			}
			break;
		}
	}
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void ProcessLightningControl(Missile &missile)
{
	missile._mirange--;

	int dam;
	if (missile.IsTrap()) {
		// BUGFIX: damage of missile should be encoded in missile struct; monster can be dead before missile arrives.
		dam = GenerateRnd(currlevel) + 2 * currlevel;
	} else if (missile._micaster == TARGET_MONSTERS) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		dam = (GenerateRnd(2) + GenerateRnd(Players[missile._misource]._pLevel) + 2) << 6;
	} else {
		auto &monster = Monsters[missile._misource];
		dam = 2 * (monster.minDamage + GenerateRnd(monster.maxDamage - monster.minDamage + 1));
	}

	SpawnLightning(missile, dam);
}

void ProcessLightning(Missile &missile)
{
	missile._mirange--;
	int j = missile._mirange;
	if (missile.position.tile != missile.position.start)
		CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._miHitFlag)
		missile._mirange = j;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessTownPortal(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._mirange > 1)
		missile._mirange--;
	if (missile._mirange == missile.var1)
		SetMissDir(missile, 1);
	if (leveltype != DTYPE_TOWN && missile._mimfnum != 1 && missile._mirange != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}

	for (Player &player : Players) {
		if (player.plractive && player.isOnActiveLevel() && !player._pLvlChanging && player._pmode == PM_STAND && player.position.tile == missile.position.tile) {
			ClrPlrPath(player);
			if (&player == MyPlayer) {
				NetSendCmdParam1(true, CMD_WARP, missile._misource);
				player._pmode = PM_NEWLVL;
			}
		}
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessFlashBottom(Missile &missile)
{
	missile._mirange--;

	constexpr Direction Offsets[] = {
		Direction::NorthWest,
		Direction::NoDirection,
		Direction::SouthEast,
		Direction::West,
		Direction::SouthWest,
		Direction::South
	};
	for (Direction offset : Offsets)
		CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
	}
	PutMissile(missile);
}

void ProcessFlashTop(Missile &missile)
{
	missile._mirange--;

	constexpr Direction Offsets[] = {
		Direction::North,
		Direction::NorthEast,
		Direction::East
	};
	for (Direction offset : Offsets)
		CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile + offset, true);

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
	}
	PutMissile(missile);
}

void ProcessFlameWave(Missile &missile)
{
	constexpr int ExpLight[14] = { 2, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 12 };

	// Adjust missile's position for processing
	missile.position.tile += Direction::North;
	missile.position.offset.deltaY += 32;

	missile.var1++;
	if (missile.var1 == missile._miAnimLen) {
		SetMissDir(missile, 1);
		missile._miAnimFrame = GenerateRnd(11) + 1;
	}
	int j = missile._mirange;
	MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
	if (missile._miHitFlag)
		missile._mirange = j;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile._mimfnum != 0 || missile._mirange == 0) {
		if (missile.position.tile != Point { missile.var3, missile.var4 }) {
			missile.var3 = missile.position.tile.x;
			missile.var4 = missile.position.tile.y;
			ChangeLight(missile._mlid, missile.position.tile, 8);
		}
	} else {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, ExpLight[0]);
		ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var2]);
		missile.var2++;
	}
	// Adjust missile's position for rendering
	missile.position.tile += Direction::South;
	missile.position.offset.deltaY -= 32;
	PutMissile(missile);
}

void ProcessGuardian(Missile &missile)
{
	missile._mirange--;

	if (missile.var2 > 0) {
		missile.var2--;
	}
	if (missile._mirange == missile.var1 || (missile._mimfnum == 2 && missile.var2 == 0)) {
		SetMissDir(missile, 1);
	}

	Point position = missile.position.tile;

	if ((missile._mirange % 16) == 0) {
		// Guardians pick a target by working backwards along lines originally based on VisionCrawlTable.
		// Because of their rather unique behaviour the points checked have been unrolled here
		constexpr std::array<WorldTileDisplacement, 48> guardianArc {
			{
			    // clang-format off
			    { 6, 0 }, { 5, 0 }, { 4, 0 }, { 3, 0 }, { 2, 0 }, { 1, 0 },
			    { 6, 1 }, { 5, 1 }, { 4, 1 }, { 3, 1 },
			    { 6, 2 }, { 2, 1 },
			    { 5, 2 },
			    { 6, 3 }, { 4, 2 },
			    { 5, 3 }, { 3, 2 }, { 1, 1 },
			    { 6, 4 },
			    { 6, 5 }, { 5, 4 }, { 4, 3 }, { 2, 2 },
			    { 5, 5 }, { 4, 4 }, { 3, 3 },
			    { 6, 6 }, { 5, 6 }, { 4, 5 }, { 3, 4 }, { 2, 3 },
			    { 4, 6 }, { 3, 5 }, { 2, 4 }, { 1, 2 },
			    { 3, 6 }, { 2, 5 }, { 1, 3 }, { 0, 1 },
			    { 2, 6 }, { 1, 4 },
			    { 1, 5 },
			    { 1, 6 },
			    { 0, 2 },
			    { 0, 3 },
			    { 0, 6 }, { 0, 5 }, { 0, 4 },
			    // clang-format on
			}
		};
		for (WorldTileDisplacement offset : guardianArc) {
			if (GuardianTryFireAt(missile, position + offset)
			    || GuardianTryFireAt(missile, position + offset.flipXY())
			    || GuardianTryFireAt(missile, position + offset.flipY())
			    || GuardianTryFireAt(missile, position + offset.flipX()))
				break;
		}
	}

	if (missile._mirange == 14) {
		SetMissDir(missile, 0);
		missile._miAnimFrame = 15;
		missile._miAnimAdd = -1;
	}

	missile.var3 += missile._miAnimAdd;

	if (missile.var3 > 15) {
		missile.var3 = 15;
	} else if (missile.var3 > 0) {
		ChangeLight(missile._mlid, position, missile.var3);
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}

	PutMissile(missile);
}

void ProcessChainLightning(Missile &missile)
{
	int id = missile._misource;
	Point position = missile.position.tile;
	Point dst { missile.var1, missile.var2 };
	Direction dir = GetDirection(position, dst);
	AddMissile(position, dst, dir, MissileID::LightningControl, TARGET_MONSTERS, id, 1, missile._mispllvl);
	int rad = std::min<int>(missile._mispllvl + 3, MaxCrawlRadius);
	Crawl(1, rad, [&](Displacement displacement) {
		Point target = position + displacement;
		if (InDungeonBounds(target) && dMonster[target.x][target.y] > 0) {
			dir = GetDirection(position, target);
			AddMissile(position, target, dir, MissileID::LightningControl, TARGET_MONSTERS, id, 1, missile._mispllvl);
		}
		return false;
	});
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void ProcessWeaponExplosion(Missile &missile)
{
	constexpr int ExpLight[10] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2 };

	missile._mirange--;
	const Player &player = Players[missile._misource];
	int mind;
	int maxd;
	DamageType damageType;
	if (missile.var2 == 1 || missile.var2 == 4) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		if (missile.var2 == 1) {
			mind = player._pIFMinDam;
			maxd = player._pIFMaxDam;
		} else if (missile.var2 == 4) {
			mind = player._pIFMinDam / 4;
			maxd = player._pIFMaxDam / 4;
		}
		damageType = DamageType::Fire;
	} else if (missile.var2 == 2 || missile.var2 == 5) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		if (missile.var2 == 2) {
			mind = player._pILMinDam;
			maxd = player._pILMaxDam;
		} else if (missile.var2 == 5) {
			mind = player._pILMinDam / 4;
			maxd = player._pILMaxDam / 4;
		}
		damageType = DamageType::Lightning;
	} else if (missile.var2 == 3 || missile.var2 == 6) {
		// BUGFIX: damage of missile should be encoded in missile struct; player can be dead/have left the game before missile arrives.
		if (missile.var2 == 3) {
			mind = player._pIMMinDam;
			maxd = player._pIMMaxDam;
		} else if (missile.var2 == 6) {
			mind = player._pIMMinDam / 4;
			maxd = player._pIMMaxDam / 4;
		}
		damageType = DamageType::Magic;
	}
	CheckMissileCol(missile, damageType, mind, maxd, false, missile.position.tile, false);
	if (missile.var1 == 0) {
		missile._mlid = AddLight(missile.position.tile, 9);
	} else {
		if (missile._mirange != 0)
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
	}
	missile.var1++;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		PutMissile(missile);
	}
}

void ProcessMissileExplosion(Missile &missile)
{
	constexpr int ExpLight[] = { 9, 10, 11, 12, 11, 10, 8, 6, 4, 2, 1, 0, 0, 0, 0 };

	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	} else {
		if (missile.var1 == 0)
			missile._mlid = AddLight(missile.position.tile, 9);
		else
			ChangeLight(missile._mlid, missile.position.tile, ExpLight[missile.var1]);
		missile.var1++;
		PutMissile(missile);
	}
}

void ProcessAcidSplate(Missile &missile)
{
	if (missile._mirange == missile._miAnimLen) {
		if (missile.sourceType() == MissileSource::Monster) {
			missile.position.tile += Displacement { 1, 1 };
			missile.position.offset.deltaY -= 32;
		} else if (missile.sourceType() == MissileSource::Player) {
			Point c = missile.position.tile;
			auto *monster = FindClosest(c, 1);
			auto *targetPlayer = FindClosestPlayer(c, 1);

			if (monster && monster->position.tile != Point { missile.var6, missile.var7 }) {
				missile.var6 = monster->position.tile.x;
				missile.var7 = monster->position.tile.y;
				missile.position.tile = monster->position.tile;
			} else if (targetPlayer && targetPlayer->position.tile != Point { missile.var6, missile.var7 } && !Players[missile._misource].friendlyMode) {
				missile.var6 = targetPlayer->position.tile.x;
				missile.var7 = targetPlayer->position.tile.y;
				missile.position.tile = targetPlayer->position.tile;
			} else {
				// The direction the player is facing
				Direction sd = Players[missile._misource]._pdir;
				missile.position.tile = c;
			}
		}
	}

	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		switch (missile.sourceType()) {
		case MissileSource::Player: {
			Player &player = Players[missile._misource];
			int dam = player._pIMMinDam + GenerateRnd(player._pIMMaxDam - player._pIMMinDam + 1);
			AddMissile(missile.position.tile, { 0, 0 }, Direction::South, MissileID::AcidPuddle, TARGET_MONSTERS, player.getId(), dam, missile._mispllvl);
			break;
		}
		case MissileSource::Monster: {
			int monst = missile._misource;
			int dam = (Monsters[monst].data().level >= 2 ? 2 : 1);
			AddMissile(missile.position.tile, { 0, 0 }, Direction::South, MissileID::AcidPuddle, TARGET_PLAYERS, monst, dam, missile._mispllvl);
			break;
		}
		}
	} else {
		PutMissile(missile);
	}
}

void ProcessTeleport(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange <= 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;
	Player &player = Players[id];

	std::optional<Point> teleportDestination = FindClosestValidPosition(
	    [&player](Point target) {
		    return PosOkPlayer(player, target);
	    },
	    missile.position.tile, 0, 5);

	if (!teleportDestination)
		return;

	dPlayer[player.position.tile.x][player.position.tile.y] = 0;
	PlrClrTrans(player.position.tile);
	player.position.tile = *teleportDestination;
	player.position.future = player.position.tile;
	player.position.old = player.position.tile;
	PlrDoTrans(player.position.tile);
	missile.var1 = 1;
	dPlayer[player.position.tile.x][player.position.tile.y] = id + 1;
	if (leveltype != DTYPE_TOWN) {
		ChangeLightXY(player.lightId, player.position.tile);
		ChangeVisionXY(player.getId(), player.position.tile);
	}
	if (&player == MyPlayer) {
		ViewPosition = player.position.tile;
	}
}

void ProcessStoneCurse(Missile &missile)
{
	missile._mirange--;
	auto &monster = Monsters[missile.var2];
	if (monster.hitPoints == 0 && missile._miAnimType != MissileGraphicID::StoneCurseShatter) {
		missile._mimfnum = 0;
		missile._miDrawFlag = true;
		SetMissAnim(missile, MissileGraphicID::StoneCurseShatter);
		missile._mirange = 11;
	}
	if (monster.mode != MonsterMode::Petrified) {
		missile._miDelFlag = true;
		return;
	}

	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		if (monster.hitPoints > 0) {
			monster.mode = static_cast<MonsterMode>(missile.var1);
			monster.animInfo.isPetrified = false;
		} else {
			AddCorpse(monster.position.tile, stonendx, monster.direction);
		}
	}
	if (missile._miAnimType == MissileGraphicID::StoneCurseShatter)
		PutMissile(missile);
}

void ProcessApocalypseBoom(Missile &missile)
{
	missile._mirange--;
	if (missile.var1 == 0)
		CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, missile.position.tile, true);
	if (missile._miHitFlag)
		missile.var1 = 1;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessRhino(Missile &missile)
{
	int monst = missile._misource;
	auto &monster = Monsters[monst];
	if (monster.mode != MonsterMode::Charge) {
		missile._miDelFlag = true;
		return;
	}
	UpdateMissilePos(missile);
	Point prevPos = missile.position.tile;
	Point newPosSnake;
	dMonster[prevPos.x][prevPos.y] = 0;
	if (monster.ai == MonsterAIID::Snake) {
		missile.position.traveled += missile.position.velocity * 2;
		UpdateMissilePos(missile);
		newPosSnake = missile.position.tile;
		missile.position.traveled -= missile.position.velocity;
	} else {
		missile.position.traveled += missile.position.velocity;
	}
	UpdateMissilePos(missile);
	Point newPos = missile.position.tile;
	if (!IsTileAvailable(monster, newPos) || (monster.ai == MonsterAIID::Snake && !IsTileAvailable(monster, newPosSnake))) {
		MissToMonst(missile, prevPos);
		missile._miDelFlag = true;
		return;
	}
	monster.position.future = newPos;
	monster.position.old = newPos;
	monster.position.tile = newPos;
	dMonster[newPos.x][newPos.y] = -(monst + 1);
	if (monster.isUnique())
		ChangeLightXY(missile._mlid, newPos);
	MoveMissilePos(missile);
	PutMissile(missile);
}

void ProcessFireWallControl(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		return;
	}

	int id = missile._misource;

	{
		Point position = { missile.var1, missile.var2 };
		Point target = position + static_cast<Direction>(missile.var3);

		if (!missile.limitReached && GrowWall(id, position, target, MissileID::FireWall, missile._mispllvl, 0)) {
			missile.var1 = target.x;
			missile.var2 = target.y;
		} else {
			missile.limitReached = true;
		}
	}

	{
		Point position = { missile.var5, missile.var6 };
		Point target = position + static_cast<Direction>(missile.var4);

		if (missile.var7 == 0 && GrowWall(id, position, target, MissileID::FireWall, missile._mispllvl, 0)) {
			missile.var5 = target.x;
			missile.var6 = target.y;
		} else {
			missile.var7 = 1;
		}
	}
}

void ProcessInfravision(Missile &missile)
{
	Player &player = Players[missile._misource];
	missile._mirange--;
	player._pInfraFlag = true;
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		CalcPlrItemVals(player, true);
	}
}

void ProcessApocalypse(Missile &missile)
{
	for (int j = missile.var2; j < missile.var3; j++) {
		for (int k = missile.var4; k < missile.var5; k++) {
			int mid = dMonster[k][j] - 1;
			if (mid < 0)
				continue;
			if (Monsters[mid].isPlayerMinion())
				continue;
			if (TileHasAny(dPiece[k][j], TileProperties::Solid))
				continue;
			if (!LineClearMissile(missile.position.tile, { k, j }))
				continue;
			int id = missile._misource;
			AddMissile({ k, j }, { k, j }, Players[id]._pdir, MissileID::ApocalypseBoom, TARGET_MONSTERS, id, missile._midam, 0);
			missile.var2 = j;
			missile.var4 = k + 1;
			return;
		}
		missile.var4 = missile.var6;
	}
	missile._miDelFlag = true;
}

void ProcessFlameWaveControl(Missile &missile)
{
	bool f1 = false;
	bool f2 = false;

	int id = missile._misource;
	Point src = missile.position.tile;
	Direction sd = GetDirection(src, { missile.var1, missile.var2 });
	Direction dira = Left(Left(sd));
	Direction dirb = Right(Right(sd));
	Point na = src + sd;
	int pn = dPiece[na.x][na.y];
	assert(pn >= 0 && pn <= MAXTILES);
	if (!TileHasAny(pn, TileProperties::BlockMissile)) {
		Direction pdir = Players[id]._pdir;
		AddMissile(na, na + sd, pdir, MissileID::FlameWave, TARGET_MONSTERS, id, 0, missile._mispllvl);
		na += dira;
		Point nb = src + sd + dirb;
		for (int j = 0; j < (missile._mispllvl / 2) + 2; j++) {
			pn = dPiece[na.x][na.y]; // BUGFIX: dPiece is accessed before check against dungeon size and 0
			assert(pn >= 0 && pn <= MAXTILES);
			if (TileHasAny(pn, TileProperties::BlockMissile) || f1 || !InDungeonBounds(na)) {
				f1 = true;
			} else {
				AddMissile(na, na + sd, pdir, MissileID::FlameWave, TARGET_MONSTERS, id, 0, missile._mispllvl);
				na += dira;
			}
			pn = dPiece[nb.x][nb.y]; // BUGFIX: dPiece is accessed before check against dungeon size and 0
			assert(pn >= 0 && pn <= MAXTILES);
			if (TileHasAny(pn, TileProperties::BlockMissile) || f2 || !InDungeonBounds(nb)) {
				f2 = true;
			} else {
				AddMissile(nb, nb + sd, pdir, MissileID::FlameWave, TARGET_MONSTERS, id, 0, missile._mispllvl);
				nb += dirb;
			}
		}
	}

	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
}

void ProcessRage(Missile &missile)
{
	missile._mirange--;

	if (missile._mirange != 0) {
		return;
	}

	Player &player = Players[missile._misource];

	int mpdif = player._pMaxMana - player._pMana;

	if (HasAnyOf(player._pSpellFlags, SpellFlag::RageActive)) {
		player._pSpellFlags &= ~SpellFlag::RageActive;
		player._pSpellFlags |= SpellFlag::RageCooldown;
		int lvl = player._pLevel * 2;
		missile._mirange = lvl + 10 * missile._mispllvl + 245;
	} else {
		player._pSpellFlags &= ~SpellFlag::RageCooldown;
		missile._miDelFlag = true;
		player._pMana = player._pMana / 2;
	}

	CalcPlrItemVals(player, true);
	RedrawEverything();
	player.Say(HeroSpeech::HeavyBreathing);
}

void ProcessInferno(Missile &missile)
{
	missile._mirange--;
	missile.var2--;
	int k = missile._mirange;
	CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, true, missile.position.tile, false);
	if (missile._mirange == 0 && missile._miHitFlag)
		missile._mirange = k;
	if (missile.var2 == 0)
		missile._miAnimFrame = 20;
	if (missile.var2 <= 0) {
		k = missile._miAnimFrame;
		if (k > 11)
			k = 24 - k;
		ChangeLight(missile._mlid, missile.position.tile, k);
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	if (missile.var2 <= 0)
		PutMissile(missile);
}

void ProcessInfernoControl(Missile &missile)
{
	missile._mirange--;
	missile.position.traveled += missile.position.velocity;
	UpdateMissilePos(missile);
	if (missile.position.tile != Point { missile.var1, missile.var2 }) {
		int id = dPiece[missile.position.tile.x][missile.position.tile.y];
		if (!TileHasAny(id, TileProperties::BlockMissile)) {
			AddMissile(
			    missile.position.tile,
			    missile.position.start,
			    Direction::South,
			    MissileID::Inferno,
			    missile._micaster,
			    missile._misource,
			    missile.var3,
			    missile._mispllvl,
			    &missile);
		} else {
			missile._mirange = 0;
		}
		missile.var1 = missile.position.tile.x;
		missile.var2 = missile.position.tile.y;
		missile.var3++;
	}
	if (missile._mirange == 0 || missile.var3 == 3)
		missile._miDelFlag = true;
}

void ProcessChargedBolt(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType != MissileGraphicID::Lightning) {
		if (missile.var3 == 0) {
			constexpr int BPath[16] = { -1, 0, 1, -1, 0, 1, -1, -1, 0, 0, 1, 1, 0, 1, -1, 0 };

			auto md = static_cast<Direction>(missile.var2);
			switch (BPath[missile._mirnd]) {
			case -1:
				md = Left(md);
				break;
			case 1:
				md = Right(md);
				break;
			}

			missile._mirnd = (missile._mirnd + 1) & 0xF;
			UpdateMissileVelocity(missile, missile.position.tile + md, 8);
			missile.var3 = 16;
		} else {
			missile.var3--;
		}
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), missile._midam, missile._midam, false, false);
		if (missile._miHitFlag) {
			missile.var1 = 8;
			missile._mimfnum = 0;
			missile.position.offset = { 0, 0 };
			missile.position.velocity = {};
			SetMissAnim(missile, MissileGraphicID::Lightning);
			missile._mirange = missile._miAnimLen;
		}
		ChangeLight(missile._mlid, missile.position.tile, missile.var1);
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

void ProcessHolyBolt(Missile &missile)
{
	missile._mirange--;
	if (missile._miAnimType != MissileGraphicID::HolyBoltExplosion) {
		int dam = missile._midam;
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), dam, dam, true, true);
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, MissileGraphicID::HolyBoltExplosion);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();
		} else {
			if (missile.position.tile != Point { missile.var1, missile.var2 }) {
				missile.var1 = missile.position.tile.x;
				missile.var2 = missile.position.tile.y;
				ChangeLight(missile._mlid, missile.position.tile, 8);
			}
		}
	} else {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame + 7);
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	}
	PutMissile(missile);
}

void ProcessElemental(Missile &missile)
{
	missile._mirange--;
	int dam = missile._midam;
	const Point missilePosition = missile.position.tile;
	if (missile._miAnimType == MissileGraphicID::BigExplosion) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);

		Point startPoint = missile.var3 == 2 ? Point { missile.var4, missile.var5 } : Point(missile.position.start);
		constexpr Direction Offsets[] = {
			Direction::NoDirection,
			Direction::SouthWest,
			Direction::NorthEast,
			Direction::SouthEast,
			Direction::East,
			Direction::South,
			Direction::NorthWest,
			Direction::West,
			Direction::North
		};
		for (Direction offset : Offsets) {
			if (!CheckBlock(startPoint, missilePosition + offset))
				CheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), dam, dam, true, missilePosition + offset, true);
		}

		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
	} else {
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), dam, dam, false, false);
		if (missile.var3 == 0 && missilePosition == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._mirange = 255;
			auto *nextMonster = FindClosest(missilePosition, 19);
			if (nextMonster != nullptr) {
				Direction sd = GetDirection(missilePosition, nextMonster->position.tile);
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, nextMonster->position.tile, 16);
			} else {
				Direction sd = Players[missile._misource]._pdir;
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, missilePosition + sd, 16);
			}
		}
		if (missilePosition != Point { missile.var1, missile.var2 }) {
			missile.var1 = missilePosition.x;
			missile.var2 = missilePosition.y;
			ChangeLight(missile._mlid, missilePosition, 8);
		}
		if (missile._mirange == 0) {
			missile._mimfnum = 0;
			SetMissAnim(missile, MissileGraphicID::BigExplosion);
			missile._mirange = missile._miAnimLen - 1;
			missile.position.StopMissile();
		}
	}
	PutMissile(missile);
}

void ProcessBoneSpirit(Missile &missile)
{
	Player &player = Players[missile._misource];
	auto &BoneArmor = player.InvBody[INVLOC_CHEST];
	missile._mirange--;
	int base = (missile._mispllvl * 3) + (player._pMagic / 2) + 4;
	int minDmg = (ScaleSpellEffect(base, missile._mispllvl) / 2);
	int maxDmg = (ScaleSpellEffect(base + 36, missile._mispllvl) / 2);
	missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;
	if (player._pIMisType == 9
	    || HasAllOf(BoneArmor._iFlags, ItemSpecialEffect::MagicDamage | ItemSpecialEffect::FastestHitRecovery)
	        && HasAnyOf(player._pIFlags, ItemSpecialEffect::Empower)) {
		if (!(player.executedSpell.spellType == SpellType::Spell
		        && player.executedSpell.spellType == SpellType::Scroll
		        && player.executedSpell.spellType == SpellType::Charges)) {
			minDmg = player._pIMMinDam;
			maxDmg = player._pIMMaxDam;
			missile._midam = GenerateRnd(maxDmg - minDmg + 1) + minDmg;
		}
	}
	if (missile._mimfnum == 8) {
		ChangeLight(missile._mlid, missile.position.tile, missile._miAnimFrame);
		if (missile._mirange == 0) {
			missile._miDelFlag = true;
			AddUnLight(missile._mlid);
		}
		PutMissile(missile);
	} else {
		MoveMissileAndCheckMissileCol(missile, GetMissileData(missile._mitype).damageType(), minDmg, maxDmg, false, false);
		Point c = missile.position.tile;
		if (missile.var3 == 0 && c == Point { missile.var4, missile.var5 })
			missile.var3 = 1;
		if (missile.var3 == 1) {
			missile.var3 = 2;
			missile._mirange = 255;
			Monster *monster = FindClosest(c, 19);
			Player *targetPlayer = FindClosestPlayer(c, 19);
			if (monster != nullptr && monster->position.tile != Point { missile.var6, missile.var7 }) {
				// If monster is available and not at the last known position
				missile.var6 = monster->position.tile.x;
				missile.var7 = monster->position.tile.y;
				SetMissDir(missile, GetDirection(c, monster->position.tile));
				UpdateMissileVelocity(missile, monster->position.tile, 16);
			} else if (targetPlayer != nullptr && !Players[missile._misource].friendlyMode) {
				// If player is available and not in friendly mode
				SetMissDir(missile, GetDirection(c, targetPlayer->position.tile));
				UpdateMissileVelocity(missile, targetPlayer->position.tile, 16);
			} else {
				// Default behavior (e.g. when neither a monster nor a player is found)
				Direction sd = Players[missile._misource]._pdir;
				SetMissDir(missile, sd);
				UpdateMissileVelocity(missile, c + sd, 16);
			}
		}
		if (c != Point { missile.var1, missile.var2 }) {
			missile.var1 = c.x;
			missile.var2 = c.y;
			ChangeLight(missile._mlid, c, 8);
		}
		if (missile._mirange == 0) {
			SetMissDir(missile, 8);
			missile.position.velocity = {};
			missile._mirange = 7;
		}
		PutMissile(missile);
	}
}

void ProcessResurrectBeam(Missile &missile)
{
	missile._mirange--;
	if (missile._mirange == 0)
		missile._miDelFlag = true;
	PutMissile(missile);
}

void ProcessRedPortal(Missile &missile)
{
	int expLight[17] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

	if (missile._mirange > 1)
		missile._mirange--;
	if (missile._mirange == missile.var1)
		SetMissDir(missile, 1);

	if (leveltype != DTYPE_TOWN && missile._mimfnum != 1 && missile._mirange != 0) {
		if (missile.var2 == 0)
			missile._mlid = AddLight(missile.position.tile, 1);
		ChangeLight(missile._mlid, missile.position.tile, expLight[missile.var2]);
		missile.var2++;
	}
	if (missile._mirange == 0) {
		missile._miDelFlag = true;
		AddUnLight(missile._mlid);
	}
	PutMissile(missile);
}

static void DeleteMissiles()
{
	Missiles.remove_if([](Missile &missile) { return missile._miDelFlag; });
}

void ProcessManaShield()
{
	Player &myPlayer = *MyPlayer;
	if (myPlayer.pManaShield && myPlayer._pMana <= 0) {
		myPlayer.pManaShield = false;
		NetSendCmd(true, CMD_REMSHIELD);
	}
}

void ProcessMissiles()
{
	for (auto &missile : Missiles) {
		const auto &position = missile.position.tile;
		if (InDungeonBounds(position)) {
			dFlags[position.x][position.y] &= ~(DungeonFlag::Missile | DungeonFlag::MissileFireWall | DungeonFlag::MissileLightningWall);
		} else {
			missile._miDelFlag = true;
		}
	}

	DeleteMissiles();

	MissilePreFlag = false;

	for (auto &missile : Missiles) {
		const MissileData &missileData = GetMissileData(missile._mitype);
		if (missileData.mProc != nullptr)
			missileData.mProc(missile);
		if (missile._miAnimFlags == MissileGraphicsFlags::NotAnimated)
			continue;

		missile._miAnimCnt++;
		if (missile._miAnimCnt < missile._miAnimDelay)
			continue;

		missile._miAnimCnt = 0;
		missile._miAnimFrame += missile._miAnimAdd;
		if (missile._miAnimFrame > missile._miAnimLen)
			missile._miAnimFrame = 1;
		else if (missile._miAnimFrame < 1)
			missile._miAnimFrame = missile._miAnimLen;
	}

	ProcessManaShield();
	DeleteMissiles();
}

void missiles_process_charge()
{
	for (auto &missile : Missiles) {
		missile._miAnimData = GetMissileSpriteData(missile._miAnimType).spritesForDirection(missile._mimfnum);
		if (missile._mitype != MissileID::Rhino)
			continue;

		const CMonster &mon = Monsters[missile._misource].type();

		MonsterGraphic graphic;
		if (IsAnyOf(mon.type, MT_HORNED, MT_MUDRUN, MT_FROSTC, MT_OBLORD)) {
			graphic = MonsterGraphic::Special;
		} else if (IsAnyOf(mon.type, MT_NSNAKE, MT_RSNAKE, MT_BSNAKE, MT_GSNAKE)) {
			graphic = MonsterGraphic::Attack;
		} else {
			graphic = MonsterGraphic::Walk;
		}
		missile._miAnimData = mon.getAnimData(graphic).spritesForDirection(static_cast<Direction>(missile._mimfnum));
	}
}

void RedoMissileFlags()
{
	for (auto &missile : Missiles) {
		PutMissile(missile);
	}
}

} // namespace devilution
