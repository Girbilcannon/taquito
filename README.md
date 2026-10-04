# Taquito
Taquito is like a super light weight yet clean version of GW2 TacO for Nexus users. Take advantage of personal or downloadable marker packs and visualize them without all the extra fluff.

### Clean and Simple Quick Menu
- Dropdowns for marker categories stay upon so you no longer lose your place.
- All submenu items are in clear text and there are no hidden options.
- Settings and online marker packs also accessible through dropdown quick menu.

### Download Blish HUD marker Packs
- Uses Blish HUD's online marker pack catalog
- Manual Check for pack updates as they become available
- Install personal packs along side downloaded packs

## Notes for Pack Developers
- All standard Category and POI attributes used by Blish HUD
- Reads both .taco and .zip packs containing the standard XML/PNG/TRL layouts

### Taquito-Specific Sticky Attributes
Taquito comes with a few attributes dedicated to marker textures being locked to the screen instead of floating in 3D. These attributes control the sticky texture visibility (disabling the floating texture), X and Y positioning from center on screen, and scale. The following list will show you how they are used. These attributes are perfect of information markers where you want the user to be able to read them clearly and should be used with really short visibility distances.

#### SCREENSTICKY
- **Attribute Use:** `screenSticky="true/false"`
- **Info:** *"true" makes the marker texture/icon stick directly do the screen at its full resolution at screen center.*

#### STICKYX
- **Attribute Use:** `stickyX="###"`
- **Info:** *Entered value can be positive or negative to affect offset left or right from screen center.*

#### STICKYY
- **Attribute Use:** `stickyY="###"`
- **Info:** *Entered value can be positive or negative to affect offset up or down from screen center.*

#### STICKYSCALE
- **Attribute Use:** `stickyScale="#.#"`
- **Info:** *Entered value should be a whole or decimal value. A good starting point is always "1.0"*

#### EXAMPLE USE
```xml
<MarkerCategory name="markerName" DisplayName="Sticky Marker" fadeNear="350" fadeFar="370" iconFile="DATA/stickyMarker.png" screenSticky="true" stickyX="560" stickyY="0" stickyScale="1.0"/>
```
These attributes can be used along side any other standard attribute. As long as screenSticky attribute is set to True, it will always override other positioning and scaling attributes. Setting to false or using a pack with these attributes in other pathing tools will simply revert the sticky markers to their other specified attribute positions. Scale and Position use screen edge safezones, meaning that if the image is too big or you set it too far to one side (like off screen), it will lock back to the edge of the screen and scale down to be fully visible.
