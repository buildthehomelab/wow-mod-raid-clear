# Raid Clear

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that teaches
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots) bots the boss and trash
mechanics of each raid, one raid at a time.

[mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear) already walks a bot raid from
boss to boss. As soon as a boss is pulled it steps back and leaves the fight to playerbots' own
raid strategies. In the TBC raids and ICC those are detailed, but in the vanilla raids most
bosses only get a resistance aura. This module fills in the missing mechanics.

## How it works

- Each raid has one strategy, named `rc <raid>`. Every bot on that raid's map gets it
  automatically and loses it when it leaves. Playerbots' own strategy for the raid keeps running
  alongside it.
- It doesn't need mod-dungeon-clear. The mechanics also apply when you lead the raid yourself.
- Neither mod-playerbots nor mod-dungeon-clear is edited. The module adds its strategies to
  playerbots at startup, the same way mod-dungeon-clear does.

### Kill order (all raids)

DPS bots always attack the skull-marked enemy first. The group's main-tank bot keeps the skull on
whichever add should die first: adds that split or multiply, then healers, then the boss's other
adds. A skull already on an add of the same or higher priority is left where it is, so the mark
doesn't flicker between equal adds. A skull you put on an add stays there too.

### Main tank and off-tanks (all raids)

`.botraid` builds raids with two or more tanks, but playerbots treats them all the same: each one
grabs whatever isn't hitting it, off-tanks taunt the main tank's mob, and every mob ends up in one
pile. In any raid:

- **One main tank.** If nobody has the group's Main Tank flag, the tank playerbots already treats
  as main gets it (first tank in group order). The main tank then sticks to its own target, and
  mod-dungeon-clear leads with the same tank. A Main Tank you set yourself is kept.
- **Off-tanks.** The other tanks never take the main tank's target. They pick up mobs hitting
  healers or DPS first, then extra mobs on the main tank, and leave mobs another off-tank already
  holds.
- **Separated on trash.** An off-tank drags its mob away from the main tank's mob, so cleaves and
  stomps hit only one tank: at least 12 yards apart, and further for mobs with a big AoE (in Molten
  Core, two Molten Giants end up 30 yards apart and two Molten Destroyers 36). It only picks dry
  ground (no lava) at about the same height and in the main tank's line of sight. In boss fights
  it stays put unless configured otherwise.
- **Co-tanking bosses that drop threat.** Some bosses knock their tank away and wipe part of its
  threat (Broodlord Lashlayer's Knock Away, the Blackwing drakes' Wing Buffet). On those, an
  off-tank with no add to hold attacks the boss too, so it sits second on threat and catches the
  boss before it turns on a healer. The main tank taunts it back.
- **Adds that need a tank of their own.** Some adds have to be tanked away from everyone (Blackwing
  Lair's Death Talon Captain). The main tank leaves them alone and drops one it already had. An
  off-tank taunts them off whoever has them, the main tank included, drags them well clear of the
  main tank's mob and keeps them until they die. With more of them than free off-tanks, the main
  tank keeps the extra ones.
- **Sharing a boss's threat.** Some bosses keep taking their tank away (Hakkar's mind control and
  threat wipes). On those, a tank the boss isn't on taunts to lift its threat back to the top: at
  once when it has lost its threat (under half of the highest), at most every 30 seconds when it
  has only drifted behind (under 90%). Any tank taunts the moment the boss is on a player who
  isn't a tank, and only one tank taunts at a time. Playerbots' damage dealers don't watch their
  threat, so on these bosses they hold their damage while they are within 10% of the threat of the
  tank who would catch the boss next. Curses, Hunter's Mark and Tranquilizing Shot still go out.
- **Endless adds are ignored.** Off-tanks don't chase adds that respawn faster than anyone can
  tank them (Blackwing Lair's Suppression Room whelps).

### Aggro leash (all raids)

When playerbots moves a bot away from something (a mob in its face, a boss's AoE, the rest of the
group for a Living Bomb), it picks a point straight behind the bot and doesn't look at what stands
there. In a raid that is often the next pack or the next boss, and the raid ends up fighting both.

So while a bot is in combat, its walks are checked against every mob nearby that isn't in the
fight but would join it: hostile, aggressive, alive, in line of sight and on the same level.

- A walk that would pass inside such a mob's aggro radius plus a buffer (6 yards), and takes the
  bot closer to it, is stopped as it starts. The check runs on the server's own map update and
  not in the bot's AI, which only looks every 100 to 500 milliseconds while the bot keeps
  running.
- After a stop the bot starts no walk for 0.7 seconds, so whatever sent it doesn't send it
  straight back.
- A walk toward the bot's target (going to the fight, or pulling the next pack) is judged more
  gently. The target itself, and any mob whose aggro radius already covers the target, don't
  count: there is no getting at the target without them. Every other pack only has to be passed
  by half the buffer: round the corner of a pack is fine, through it is not.
- Any other walk (backing off, spreading out, dodging) is judged against every idle mob: the
  full buffer, or half of it for a mob the target stands next to. A boss tanked beside an idle
  pack doesn't make that pack safe to back into.
- A bot standing within 2 yards of an idle mob's aggro radius (a patrol walking up) steps back
  out, toward the main tank where it can. A bot in melee with its target is never moved off it.

The aggro radius is the core's own: the mob's detection range (20 yards unless its template says
otherwise) plus a yard for every level the mob is above the bot.

Knockbacks, charges and fears are not walks the bot chose and are left alone.

### Death log (all raids)

Every death in a raid gets one line in the server log, so a bad night can be read back and the
mechanic that's killing the bots found:

```
[RaidClear] death: Name (Mage, dps, bot) map 469 instance 1 | Chromaggus 41% [Frenzy, ...] 22 yd, in LOS | killed by Incinerate (Chromaggus) 3120 | last 6s: Incinerate (Chromaggus) 3120; melee (Chromaggus) 2x 1850 | debuffs: Brood Affliction: Black, ...
[RaidClear] Chromaggus killed (map 469 instance 1): 24 deaths | Incinerate (Chromaggus) 9, melee (Chromaggus) 6, ...
```

Damage is what was actually dealt. A hit whose spell can't be matched shows as `unknown`.
`grep "\[RaidClear\]" Server.log` pulls them out.

## Raids

### Molten Core

Playerbots already handles Living Bomb and Inferno, Shazzrah's Arcane Explosion, the Golemagg /
Core Rager tank split (the off-tank split stays out of that fight), Core Hound packs, lava pools and the resistance auras. On top of that:

| Fight | What the bots do |
|---|---|
| Firelord trash | Kill **Lava Spawn** before anything else (they split every 15 seconds). |
| Lucifron, Gehennas, Garr | Kill the Flamewaker Protectors, Flamewalkers and Firesworn before the boss. |
| Garr | A **Firesworn** more than 40 yards from Garr gets **Separation Anxiety** (four times the damage), and every Firesworn erupts when it dies: about 2,000 fire damage to everyone within 15 yards, or 8,000 with Separation Anxiety. So the fight stays with Garr. A bot more than 30 yards from him walks back to 22, and the Firesworn on it follows (a tank only comes when a Firesworn is on it, so trash pulled with the boss stays where its tank holds it). Nobody but the tanks hits a Firesworn that has Separation Anxiety or is far enough out to get it: bots already attacking one stop, pets and Auto Shot included, and the skull never goes on one. |
| Magmadar | Every shaman keeps **Tremor Totem** down against **Panic**. |
| Sulfuron Harbinger | Kill the Flamewaker Priests first. |
| Majordomo Executus | Healers first, then Elites. No spells into **Magic Reflection**; melee stop swinging into **Damage Reflection** (tanks keep going). |
| Ragnaros | Ranged and healers stay out of **Wrath of Ragnaros** range. Kill the **Sons of Flame** when he submerges. |

### Blackwing Lair

Playerbots already handles the Onyxia Scale Cloak, Razorgore's egg phase, Vaelastrasz's Burning
Adrenaline, Chromaggus's Bronze affliction, Nefarian's Wild Magic and Fear Ward, Wyrmguard spacing
and the fire resistance auras. On top of that:

| Fight | What the bots do |
|---|---|
| Razorgore | Kill his adds in order: **Death Talon Dragonspawn**, then **Blackwing Mages**, then **Legionnaires**. The off-tank split stays out of the fight (playerbots places the tanks). |
| Vaelastrasz | He can't be taunted, so the off-tank keeps itself second on threat for when **Burning Adrenaline** kills the main tank. |
| Warlock packs | Kill the **Blackwing Warlocks** first: each one keeps opening Demon Portals that summon Enraged Felguards until it dies. Then Taskmasters and Spellbinders, then the felguards. |
| Death Talon packs (Hall of the Dragonspawn) | The **Death Talon Captain** puts **Mark of Detonation** on whoever he hits, and every melee hit on that player then explodes on all of that player's allies within 30 yards. So he gets an off-tank of his own: the main tank leaves him alone, an off-tank taunts him off whoever has him and holds him 36 yards from the main tank's mob until he dies. Every other non-tank stays 32 yards from anyone carrying the Mark (moving out toward the main tank) and doesn't walk back toward a target inside that circle. Kill order: **Wyrmkin** first (**Fireball Volley** hits everyone within 45 yards), then **Flamescales** and **Seethers**, the Captain last, mostly by ranged; melee join in whenever the Mark is dispelled. The nearest hunter **Tranquilizing Shots** an enraged Seether. |
| Wyrmguard packs | Every **Death Talon Wyrmguard** gets its own tank. As soon as a pack is pulled (like the three before Ebonroc), the main tank keeps one and each off-tank runs in, taunts another off whoever has it and holds it. Then they spread 30 yards from each other (**War Stomp** reaches 15): the first off-tank drags its Wyrmguard away from the main tank's, the next one away from both. With fewer tanks than Wyrmguards the main tank keeps the extra ones. |
| Technician packs | Ranged bots and healers keep 6 yards from each other while **Blackwing Technicians** are fighting nearby, so one **Bomb** (5-yard splash) hits one bot. Only bots in a clump move, one short step every 1.5 seconds. |
| Suppression Room | With the `raid` bot cheat on (the default), bots turn off every armed **Suppression Device** within 22 yards: the aura reaches 20, playerbots' own disarm only 15. Kill the Hatchers and Taskmasters before Broodlord if they come along. Off-tanks ignore the whelps. |
| Broodlord Lashlayer | The off-tank co-tanks him through **Knock Away**. Ranged and healers stay out of **Blast Wave** (20 yards); a healer only backs off as far as it can still reach the tank. |
| Firemaw, Ebonroc, Flamegor | The off-tank co-tanks through **Wing Buffet**. |
| Firemaw | At 5-7 stacks of **Flame Buffet** (varies per bot, so the raid doesn't leave together) a non-tank hides behind cover until the stacks drop. At most a third of the healers hide at once. Hidden bots keep healing and casting at anything they can see. |
| Chromaggus | When he starts a breath, non-tanks with cover within a 2-second run duck behind it. **Time Lapse** is the exception: everyone takes it, because it halves the threat of everyone it hits, tank included. **Brood Affliction: Black** (more fire damage taken) comes off before anything else: mages, druids and Cleanse Spirit shamans split the cursed players between them, tanks first, then the lowest on health; healers leave it to the others while a tank is below half health. Bronze is already handled by playerbots (Hourglass Sand). |
| Nefarian | Ranged and healers stay out of **Bellowing Roar** (35-yard fear), healers only as far as they can still reach the tank. Kill the **Drakonids** and **Bone Constructs** before him. Class calls: priests stop healing while **Corrupted Healing** is on them (their heals would put a shadow DoT on the tanks; shields still go out). Priests still heal a tank below 35%. After the shaman call, the corrupted **Healing Stream** totem dies before anything else, then Stoneskin and Windfury; everyone but the tanks walks 13 yards away from the **Fire Nova** totem before it goes off. The warlock call's **Corrupted Infernals** die with the drakonids. |
| Onyxia Scale Cloak | Playerbots gives its bots the cloak's protection in Blackwing Lair, but only re-adds it every few seconds after a rez. A bot revived here gets it back the moment it stands up, so it can't be caught by a **Shadow Flame** in between. |
| Ebonroc | The off-tank taunts him off a tank with **Shadow of Ebonroc** (he heals on every hit on it), and that tank leaves him alone until it wears off. |

On older cores, a Suppression Device a player disarms with Disarm Trap disappears but keeps
slowing the raid. The core fixes this from Playerbot core `2a2211c` onward ([azerothcore-wotlk#27774](https://github.com/azerothcore/azerothcore-wotlk/pull/27774)):
the disarmed device retracts and re-arms after 30-120 seconds.

### Zul'Gurub

Playerbots has no Zul'Gurub strategy, so everything here is new.

| Fight | What the bots do |
|---|---|
| High Priest Venoxis | Kill his four **Razzashi Cobras** first. While he's a troll (above 50%), **Holy Wrath** hits his target and chains to the nearest player within 10 yards, over and over, about 30% harder each jump: nine jumps in, it hits for more than 40,000. So the chain gets nowhere to go. Everyone but the tanks stays about 16 yards away from his target, from whoever is top of his threat (Holy Wrath starts there, even if he's still hitting the tank), and from any tank or pet standing within a jump of those. Melee don't walk in at all; ranged and healers work from range. At 50% he turns into a snake, Holy Wrath stops and melee go in. |
| Hakkar | He keeps taking his tank away: **Cause Insanity** mind-controls whoever is top of his threat for 10 seconds, and for every High Priest still alive he has that priest's Aspect. The **Aspects of Arlokk and Mar'li** stun his tank and wipe its threat (Arlokk's every 10-15 seconds). Arlokk only appears when someone rings the Gong of Bethekk, so a bot raid usually reaches Hakkar with her Aspect on him. Each time he turns to whoever is next on threat, and once he has **Frenzy** he kills a mage in two swings. So the tanks share his threat and the damage dealers stay under it (see *Sharing a boss's threat* above): the next in line is always a tank. It needs two tanks or more; three is safer with a priest alive. Killing all five priests first is still the easy way: each one alive also gives him 20% more damage and 10% more health. |

## Requirements

- An AzerothCore WotLK server running [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
  on its core fork
  ([mod-playerbots/azerothcore-wotlk](https://github.com/mod-playerbots/azerothcore-wotlk),
  `Playerbot` branch). The module uses playerbots APIs directly, so the stock core won't build it.
- `AiPlayerbot.ApplyInstanceStrategies = 1` in the playerbots config (the default), so the bots
  keep playerbots' own raid strategies alongside this module's.
- Optional: [mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear), which walks a bot
  raid from boss to boss. The mechanics also apply when you lead the raid yourself.
- WoW 3.3.5a (12340) client.

## Install

1. Clone into `modules/mod-raid-clear`. The folder name must be exactly that, because
   AzerothCore derives the loader function from it:

   ```bash
   git clone https://github.com/buildthehomelab/wow-mod-raid-clear.git modules/mod-raid-clear
   ```

2. Re-run CMake and rebuild the worldserver.
3. Copy `conf/mod_raid_clear.conf.dist` to `mod_raid_clear.conf` to change the defaults.

Requires mod-playerbots with `AiPlayerbot.ApplyInstanceStrategies = 1` (the default), so the bots
also keep playerbots' own raid strategies.

## Config

| Option | Default | |
|---|---|---|
| `RaidClear.Enable` | 1 | Master switch (startup only). |
| `RaidClear.KillOrder` | 1 | Skull kill order. |
| `RaidClear.Tanks.AssignMainTank` | 1 | Give the group's Main Tank flag to a tank if nobody has it. |
| `RaidClear.Tanks.Split` | 1 | Off-tanks leave the main tank's target and pick up the rest. |
| `RaidClear.Tanks.Separation` | 12 | Minimum yards between the two tanks' mobs on trash; big-AoE mobs get more (0 = off). |
| `RaidClear.Tanks.SeparateOnBosses` | 0 | Also separate during boss fights. |
| `RaidClear.DeathLog` | 1 | Log every raid death (cause, last 6s of damage, debuffs, boss) and a per-pull tally. |
| `RaidClear.AggroLeash` | 1 | Bots in combat don't back into packs that aren't in the fight. |
| `RaidClear.AggroLeash.Buffer` | 6 | Yards kept beyond a mob's aggro radius. |
| `RaidClear.MoltenCore.Enable` | 1 | Molten Core strategy. |
| `RaidClear.BlackwingLair.Enable` | 1 | Blackwing Lair strategy. |
| `RaidClear.ZulGurub.Enable` | 1 | Zul'Gurub strategy. |
| `RaidClear.OnyxiasLair.Enable` | 1 | Onyxia's Lair strategy. |

## Patch Notes: Raid Clear

Category: Raids

- Bot raids now know more of Molten Core's and Blackwing Lair's boss mechanics.
- Bot raids now have a main tank and off-tanks. Off-tanks pick up the mobs the main tank isn't
  holding and tank them a few steps away, so one cleave doesn't hit both tanks.
- Bots kill the dangerous adds first: **Lava Spawn**, then the Flamewaker healers and priests,
  then each boss's guards, before turning to the boss.
- Shamans drop **Tremor Totem** for Magmadar's **Panic**.
- Bots stop attacking Majordomo's adds while **Magic Reflection** or **Damage Reflection** is up.
- Ranged bots and healers keep out of reach of **Wrath of Ragnaros**.
- Blackwing Lair: bots kill the **Blackwing Warlocks** before the felguards their portals keep
  summoning, then the Taskmasters and Spellbinders.
- Blackwing Lair: bots turn off **Suppression Devices** within the aura's full reach, not just the
  ones right next to them.
- Broodlord Lashlayer, Firemaw, Ebonroc and Flamegor: the off-tank builds threat on the boss, so
  **Knock Away** and **Wing Buffet** no longer send the boss onto the healers.
- Broodlord Lashlayer: ranged bots and healers stay out of **Blast Wave**.
- Ebonroc: bot tanks swap on **Shadow of Ebonroc**.
- Blackwing Lair: ranged bots and healers spread out against the Technicians' **Bombs**.
- Razorgore: bots kill the Dragonspawn, then the Mages, then the Legionnaires.
- Vaelastrasz: the off-tank keeps up threat so it can take over when **Burning Adrenaline** kills the
  main tank.
- Blackwing Lair: bots kill the Death Talon casters first and tank the Wyrmguards well apart.
- Blackwing Lair: the three **Death Talon Wyrmguards** before Ebonroc no longer all land on the
  main tank. Three tanks run in and each taunts one of its own, then they spread 30 yards apart.
- Chromaggus: mages, druids and shamans remove **Brood Affliction: Black** before anything else,
  so his fire breaths stop one-shotting the cursed.
- Raids: every death is written to the server log with what killed it, so wipes can be traced to
  the mechanic behind them.
- Nefarian: priests stop healing while **Corrupted Healing** is on them, so the tanks no longer
  die to their own healers (a tank below 35% still gets healed). Bots kill the **corrupted
  totems** from the shaman call, walk away from the **Fire Nova** totem before it goes off, and
  kill the warlock call's **Infernals** with the drakonids.
- Blackwing Lair: bots revived mid-fight get their Onyxia Scale Cloak protection back at once, so
  a **Shadow Flame** can't catch them in the gap.
- Firemaw: bots hide behind cover to drop **Flame Buffet** stacks, a few at a time, with healers
  taking turns.
- Chromaggus: bots duck behind cover when he breathes, except for **Time Lapse**.
- Nefarian: ranged bots and healers stay out of **Bellowing Roar**, and bots kill his adds first.
- Blackwing Lair, Death Talon packs: an off-tank takes the **Death Talon Captain** away from the
  raid, and everyone else stays out of the **Mark of Detonation** explosion. Bots kill the Wyrmkin
  first and the Captain last, and hunters **Tranquilizing Shot** enraged Seethers.
- Zul'Gurub, High Priest Venoxis: bots no longer let **Holy Wrath** chain through the whole raid.
  Everyone but the tanks keeps about 16 yards from his target while he's a troll, melee wait for
  his snake form, and his cobras die first.
- Zul'Gurub, Hakkar: the tanks now share his threat and damage dealers stay below them, so when he
  mind-controls his tank or wipes its threat he goes to another tank and not to a mage.
- Onyxia: bots get out of **Deep Breath** again. They used to run to fixed spots that are now in
  the fire for two of her eight breaths; now each bot walks out of the strip she is about to burn
  and stays out until it has passed.
- Onyxia: only the tank she is on and the main tank stand in front of her. The other tanks stand
  at her side and no longer take every **Flame Breath**, **Cleave** and **Wing Buffet**.
- Onyxia: bots leave a whelp that is evading every attack and no longer pick one, so the raid
  doesn't spend half the air phase on a whelp it can't hurt. Hunters keep **Hunter's Mark** for
  Onyxia.
- Raids: bots no longer back into the next pack or the next boss when they move away from
  something. A bot stops before it reaches an idle mob's aggro range, and steps away from a
  patrol that walks up to it.
- Garr: bots keep the fight within 30 yards of him and leave a **Firesworn** with **Separation
  Anxiety** alone, so its **Eruption** no longer kills everyone around it.

> Most vanilla bosses only had a resistance aura in the bots' playbook. This is the first raid
> of many.

### Onyxia's Lair

Playerbots' own strategy keeps bots off her head and tail, away from the whelp eggs, apart for
**Fireball** and on the whelps while she flies. This module takes over the Deep Breath and fills
three gaps. It works for Onyxia as the core spawns her and for
[mod-individual-progression](https://github.com/ZhengPeiRu21/mod-individual-progression)'s level 60
Onyxia.

| Fight | What the bots do |
|---|---|
| Onyxia, in the air | **Deep Breath** burns a strip 36 yards wide from one side of the room to the other, along one of eight lines. Playerbots sends its bots to two fixed safe spots per breath. Since Playerbot core `2a2211c` moved the strips, those spots are in the fire for two of the eight breaths. Here the strip is read from the server's own spell data instead: a bot standing in it drops its spell and walks to the nearest spot outside (never into the whelp caves or the egg trench), a bot outside stays where it is and keeps fighting, and nobody walks into or across the strip until the fire has passed. |
| Onyxia, on the ground | Playerbots keeps bots away from her head and tail, but leaves out every tank, so a raid's second, third and fourth tank stand in front of her and take each **Flame Breath**, **Cleave** and **Wing Buffet** with the main tank. Here only the tank she is on and the main tank stay in front. Every other tank that is fighting her walks to her nearer side (one holding a whelp stays with the whelp). If she turns to one of them while the main tank is at her, it walks over to the main tank, so she faces the way she did before and not the bots at her side. |
| Whelps and trash | A mob that can't find a way to the player it is after evades every attack, but goes on hitting. Playerbots doesn't look at that: bots keep attacking it and tanks keep taunting it. Here playerbots' target picking skips an evading mob, a bot already on one switches to what playerbots picks next (else the nearest enemy it can hurt, else Onyxia), and off-tanks don't try to pick one up (in every raid, with `RaidClear.Tanks.Split`). Playerbots' whelp rule for the core's Onyxia, which takes the first whelp it finds and never lets go of it, is replaced by one that takes the nearest whelp that isn't evading. The level 60 Onyxia's whelps are a different creature, which that rule never covered: bots pick those as they pick any other enemy. |
| Whelps | Hunters don't put **Hunter's Mark** on whelps, which die within seconds. |

## Troubleshooting

- **Bots don't use any of the raid mechanics.** Check that `AiPlayerbot.ApplyInstanceStrategies`
  is `1`, that `RaidClear.Enable` and the raid's own setting (`RaidClear.MoltenCore.Enable`,
  `RaidClear.BlackwingLair.Enable`, `RaidClear.ZulGurub.Enable`, `RaidClear.OnyxiasLair.Enable`) are `1`, and restart the worldserver: `RaidClear.Enable` is
  read at startup only.
- **The worldserver doesn't start or the module doesn't link.** The folder must be named exactly
  `mod-raid-clear`, because AzerothCore derives the loader function from it. Re-run CMake after
  cloning.
- **Off-tanks don't pull their mob away during a boss fight.** `RaidClear.Tanks.SeparateOnBosses`
  is `0` by default; separation only happens on trash.

## Credits

Built on [mod-dungeon-clear](https://github.com/jrad7/mod-dungeon-clear) by jrad7, whose way of
adding strategies to playerbots this module follows, and on
[mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Author: [buildthehomelab](https://github.com/buildthehomelab)

## License

MIT. See [LICENSE](LICENSE).
