# Taquito pathing attribute support (0.0.1.16)

Reference: [GW2 Pathing attribute guide](https://gw2pathing.com/docs/marker-dev/attributes/achievement/)
and its linked attribute pages. The guide describes TacO, Blish HUD Pathing,
TaimiHUD, and Burrito. The GitHub Pathing repository was not accessible through
the selected connector during this update, so the guide is the behavior source.

The parser now retains every XML attribute, including unfamiliar ones, under a
case-insensitive name in `Style::attributes`. Values on `MarkerCategory` are
inherited by child categories and entries; values on a `POI` or `Trail` override
them. `TacoPack::Attribute(style, name)` reads the effective original string.
**Registered** means preserved and queryable; it does not imply its behavior is
active. Unrecognized attributes remain available to later versions.

| Attribute group | XML names | Taquito behavior |
| --- | --- | --- |
| Category structure | `name`, `DisplayName`, `defaulttoggle`, `isHidden`, `isSeparator`, `type` | Menu hierarchy, defaults, hidden categories, header separators, entry matching; user toggles persist. |
| Geometry and assets | `MapID`, `xpos`, `ypos`, `zpos`, `GUID`, `iconFile`, `texture`, `trailData` | Map assignment, coordinates, asset lookup, trail parsing; GUID is retained for future state handling. |
| World and map drawing | `alpha`, `animSpeed`, `color`, `heightOffset`, `iconSize`, `mapDisplaySize`, `minSize`, `maxSize`, `scaleOnMapWithZoom`, `trailScale`, `isWall` | Applied. Wall trails stand vertically. `animSpeed=0` stops motion. Color supports `#RGB`, `#RRGGBB`, and `#RRGGBBAA` with the last two digits treated as opacity. |
| Visibility and fading | `fadeNear`, `fadeFar`, `miniMapVisibility`, `mapVisibility`, `inGameVisibility`, `canFade` | Applied. `canFade=0` exempts an item from Taquito's player visibility bubble; it does not bypass `fadeNear`/`fadeFar`. |
| Mumble context | `maptype`, `mount`, `profession`, `race`, `specialization` | Filtered from current Mumble context/identity. Comma-separated choices match without regard to case. When identity is unavailable, identity filters are left unfiltered. |
| Activation/state | `autoTrigger`, `behavior`, `invertbehavior`, `resetLength`, `resetguid`, `triggerRange`, `bounce`, `bounce-height`, `bounce-duration`, `bounce-delay` | Registered. Triggering, state persistence, and bounce animation are not yet implemented. |
| Account progress | `achievementId`, `achievementBit`, `raid` | Registered. No account API authorization or completion filter yet. |
| Time and festival | `festival`, `schedule`, `schedule-duration` | Registered. No festival feed or cron scheduler yet. |
| Interactions | `copy`, `copy-message`, `toggle`, `toggleCategory`, `show`, `hide` | Registered. No clipboard write or marker interaction hook yet. |
| More presentation | `info`, `inforange`, `tip-name`, `tip-description`, `rotate`, `rotate-x`, `rotate-y`, `rotate-z`, `cull`, `occlude` | Registered. No text popup, map tooltip, static 3D rotation, culling, or overlay occlusion yet. |
| Scripting | `script-tick`, `script-focus`, `script-trigger`, `script-filter`, `script-once` | Registered as text only. Taquito does not run pack supplied Lua. |

`maptype` accepts the guide's alternate WvW spellings such as `center` and
`eternalbattlegrounds`, `bluehome` and `blueborderlands`, and `jumppuzzle` and
`obsidiansanctum`. As in the guide, these aliases designate the same Mumble map
type. The renderer still uses the trail file's embedded map ID; an optional
`MapID` on a trail is retained but is not used to skip file parsing yet.

## Equivalent names and related attributes

| Names | Relationship |
| --- | --- |
| `toggle`, `toggleCategory` | True aliases. The latter is TacO's older spelling; `Attribute(style, "toggle")` accepts either. If both appear, `toggle` takes precedence. Interaction is not implemented yet. |
| `rotate="x,y,z"`, `rotate-x`, `rotate-y`, `rotate-z` | Two ways Blish HUD specifies the same three axis rotation. Taquito preserves both; static rotation is pending. |
| `show`, `hide`, `toggle` | Related category controls, **not** aliases: show enables, hide disables, toggle reverses. |
| `info`/`inforange`, `copy`/`copy-message`, `fadeNear`/`fadeFar`, `schedule`/`schedule-duration`, `achievementId`/`achievementBit` | Paired value and modifier, **not** duplicate attributes. |
| `maptype="center"`/`"eternalbattlegrounds"`, `"bluehome"`/`"blueborderlands"`, `"greenhome"`/`"greenborderlands"`, `"redhome"`/`"redborderlands"`, `"jumppuzzle"`/`"obsidiansanctum"` | Alternate *values* for the same Blish map filter, not separate XML attributes. |

Most shared attributes use the **same spelling** across TacO, Blish HUD,
TaimiHUD, and Burrito. The differences in the guide are usually whether a
viewer supports an attribute, rather than a duplicate name. For example,
`isWall` is supported by Blish HUD and TaimiHUD, while `rotate` is a Blish HUD
extension.

## Taquito-only sticky texture attributes

These can be put on a `MarkerCategory` or individual `POI`. Other viewers may
ignore unknown attributes, but verify in the specific viewer before shipping.

| Name | Default | Description |
| --- | --- | --- |
| `screenSticky` | `false` | Pins the marker's `iconFile` image to the screen while the player is near it, instead of drawing a camera-facing 3D billboard. Turning it off restores regular world rendering. The closest eligible sticky image is selected. |
| `stickyX` | `0` | Horizontal pixel offset from screen center. Positive moves right; negative moves left. The whole image is clamped inside the screen. |
| `stickyY` | `0` | Vertical pixel offset from screen center. Positive moves up; negative moves down. The whole image is clamped inside the screen. |
| `stickyScale` | `1` | Multiplier of original texture pixel dimensions. `0.5` is half size. The image is reduced further when necessary to fit the screen, without changing aspect ratio. |

Sticky images use `fadeNear`, `fadeFar`, and `alpha` for world distance/opacity,
respect category toggles and `inGameVisibility`, and are not affected by the
player visibility bubble. They are hidden while the full map is open. The
normal map/minimap icon remains subject to its own visibility switches.
