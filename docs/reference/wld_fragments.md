> **HISTORICAL SNAPSHOT** — point-in-time document from a completed batch or past audit; findings inside may be superseded and are NOT kept current. Current truth lives in `docs/decisions.md`, `docs/module_map.md`, and `refactor/deferred.md`.

EQWldData.pas in the OpenZone source is a direct translation of the corresponding ZoneConverter file, but also has bug fixes and improvements.

Here's a rundown of the fragment types as I understand them and some basic documentation below:

0x03 - Texture Bitmap Name(s)
0x04 - Texture Bitmap Info
0x05 - Texture Bitmap Info Reference
0x06 - Sprite 2D Definition (gequip.s3d only — billboard sprites for items)
0x07 - Sprite 2D Definition Reference
0x08 - Camera
0x09 - Camera Reference
0x10 - Track Set (Skeleton Hierarchy)
0x11 - Track Set Reference
0x12 - Mob Track/Object Track (Bone Animation Keyframes — N frames of quat rotation + translation + scale)
0x13 - Mob Track Reference/Object Track Reference
0x14 - Placeable Reference/Player Info (Actor Definition)
0x15 - Object Location (Actor Instance)
0x16 - Zone Unknown
0x17 - Polygon Hierarchy Definition (POLYHDEF) — standalone float-vertex polygon mesh, NOT animation
0x18 - Polygon Hierarchy Definition Reference
0x1B - Light Source
0x1C - Light Source Reference
0x21 - BSP Tree
0x22 - BSP Region
0x26 - Particle Sprite Definition (_SPB suffix — sprite base for particle systems)
0x27 - Particle Sprite Definition Reference
0x28 - Light Info
0x29 - Region Flag
0x2A - Ambient Light
0x2C - Legacy Mesh (uncompressed float vertices — older character models)
0x2D - Mesh Reference
0x2F - Mesh Animated Vertices Reference
0x30 - Texture (Material Definition)
0x31 - Texture List (Material List)
0x32 - Vertex Color (per-instance baked vertex lighting)
0x33 - Vertex Color Reference
0x34 - Particle Cloud Definition (_PCD suffix — particle emitter parameters)
0x35 - First Fragment (Global Ambient Light)
0x36 - Mesh
0x37 - Mesh Animated Vertices (DMTRACKDEF — per-vertex animation for zone objects)

There are other fragment types in the ZoneConverter source, but I haven't encountered them. Here I talk about all of the ones I've seen.



Misc
----

0x35 - First Fragment - For some reason every .WLD file I've ever encountered begins with one of these. I have no idea why or whether this is necessary.

Textures
--------

0x03 - Texture Bitmap Name(s) - Contains the names of one or more bitmaps used in a particular texture. When there is more than one bitmap in the fragment it means that the texture is animated (e.g. water).

0x04 - Texture Bitmap Info - Refers to a 0x03 fragment. Also contains flags to tell the client information about this particular texture (normal or animated).

0x05 - Texture Bitmap Info Reference - Refers to a 0x04 fragment so 0x04 fragments can be reused.

0x30 - Texture - Refers to a 0x05 fragment. Contains flags to tell the client information about this texture (normal, semitransparent, transparent, masked, etc.) Having this fragment separated from the 0x05 fragment means that the zone can have multiple flavors of the same texture bitmap (e.g. one that is normal, one that is semitransparent, etc.)

0x31 - Texture List - Contains references to all of the 0x30 textures used in the zone (or, in the case of placeable objects, in that particular object).


Meshes
------

0x36 - Mesh - Contains vertex, normal, color, and polygon information for a mesh. In the case of zones, the mesh is a subdivision of the zone. In the case of placeable objects, the mesh contains the entire information for the object.

Notes on 0x36 fragment:

1. Fragment1 refers to a 0x31 fragment to tell the client what textures are used.
2. Polygons are sorted by texture index. That is, all polygons in the Data5 area that use a particular texture are grouped together.
3. Fragment2 optionally refers to a 0x2F fragment if the mesh is animated (e.g. trees or flags that sway in the breeze).
4. Fragment4 always refers to the first 0x03 fragment in the file (I have no idea why).
5. I don't fully understand this fragment. The Data6 and Data9 areas have something to do with mob models, but I don't know how they work yet.
6. There are new-format and old-format .WLD files. They have different values in the .WLD header and the main difference is in the 0x36 fragment. In new-format files, the texture coordinate values are signed 32-bit values; in old-format files they're signed 16-bit values. At this time OpenZone only exports old-format files but it would be no great effort to switch it to new-format files.

0x37 - Mesh Animated Vertices - For a given 0x36 fragment, this fragment contains a number of animation "frames". For each frame it contains a complete vertex list that supercedes the vertex list in the 0x36 fragment. For instance, if there are three frames and 15 vertices, there will be three sets of 15 vertex values in the 0x37 fragment and they will be used in lieu of the 15 vertex values in the 0x36 fragment.

0x2F - Mesh Animated Vertices Reference - Refers to a 0x37 fragment so it can be reused (e.g. for flags that all have the same shape but have different textures).


Zones
-----

0x21 - BSP Tree - BSP stands for "binary space partition". Basically the zone is broken up into regions so the client can quickly find polygons that are near the player or mobs for purposes of collision avoidance. Normally this is done along a simple grid, but the zone also has to be broken up so that "special" regions (water, lava, PvP, ambient lighting, and others) are distinct.

0x22 - BSP Region - For each node in the BSP tree there is a 0x22 fragment that describes the region. It contains information on the region's bounds and an optional reference to a 0x36 fragment if there are polygons in that region. Also contains an RLE-encoded array of data that tells the client which regions are "nearby". That way, if a player enters one of the "nearby" regions, the client knows that objects in the region are visible to the player. The client does this to know when it has to make sure mobs fall to the ground instead of stay at the spawn points, which might be in midair.

0x1B - Light Source - I suspect this defines the ambient light level in a zone (see "Light sources" below for info).

0x08 - Camera - I don't know what the parameters mean yet.

0x1C - Light Source Reference - See "Light sources" below for info.

0x2A - Ambient Light - Refers to a 0x1C fragment. Contains a list of numbers that refer to the 0x22 fragments in the zone (e.g. if there are 100 0x22 fragments then the numbers will be in the range 0-99). This fragment tells the client which regions have the particular light setting. I suspect that you could conceivably have some regions with one ambient light setting, other regions with another ambient light setting, etc. by having multiple 0x1B-0x1C-0x2A sets.

0x09 - Camera Reference - Refers to a 0x08 fragment. I don't know its purpose.

0x14 - Player Info - I don't know its purpose. Its Fragment1 value seems to use a "magic" string: "FLYCAMCALLBACK".

0x16 - Zone Unknown - It's used in zone files for some reason...

0x15 - Object Location - In zone files, this might contain the safe point?

0x29 - Region Flag - This is similar to the 0x2A fragment in that it contains a list of numbers, where each number refers to a 0x22 region. It tells the client that those regions are "special". The name of the fragment is "magic" in that it determines how the regions are flagged:

"WT_ZONE" .................... Regions are underwater

"LA_ZONE" ................... Regions are lava

"DRP_ZONE" ................... Regions are PvP areas

"DRNTP##########_ZONE" ....... e.g. DRNTP00025-02698-645.6-00020999_ZONE. This seems to tell the client that these regions constitute a zoneline. If the player enters one of these regions the client knows the player is zoning and knows the destination. I don't know if the client makes use of this since I don't think every zone has this at all zone points, but it looks interesting. I don't understand the format of the numbered part of the name.


Mob models
----------

0x12 - Mob Track - I don't know what this does, but I suspect it has to do with mob animation.

0x13 - Mob Track Reference - Refers to a 0x12 fragment.

0x2D - Mesh Reference - Refers to a 0x36 fragment. I don't know its purpose.


Light sources
-------------
0x1B - Light Source - Contains information on light color. I don't know what the other parameters in it mean.

0x1C - Light Source Reference - Refers to a 0x1B fragment.

0x28 - Light Info - Refers to a 0x1C fragment. Contains light position and radius. I don't know what the other parameters mean.


Placeable objects
-----------------
0x32 - Vertex Color - For each object that has been placed, there is one of these (put 100 trees in your zone and there are 100 of these fragments). It contains vertex shading information for each object. For example, if you have a torch near some trees, those trees should have their polygons shaded based on the light color, angle of incidence, distance, and any intervening polygons. The EQ client does *not* dyamically shade polygons in a zone; all polygons must be shaded in this way (including 0x36 fragments in the main zone file).

0x33 - Vertex Color Reference - Refers to a 0x32 fragment.

0x15 - Object Location - Contains object position, rotation, and size. Refers to a 0x33 fragment. When used in the main zone file, this fragment contains information for the whole zone (see frmMainUnit.pas in the OpenZone source).

0x17 - Polygon Animation? - I don't know what this does. I suspect it has something to do with polygon animation.

0x18 - Polygon Animation Reference? - Refers to a 0x17 fragment. I don't know its purpose.

0x2D - Mesh Reference - Refers to the 0x36 fragment.

0x12 - Object Track - I don't know what this does, but I suspect it has to do with object animation.

0x13 - Object Track Reference - Refers to a 0x12 fragment.

0x10 - Track Set - This seems to be optional for placeable objects. My guess is that it allows a placeable object to have more than one animation track. For each track that is used there is a reference to a 0x13 fragment. There might (though not necessarily) also be a reference to a 0x2D fragment for each track.

0x11 - Track Set Reference - Refers to a 0x10 fragment.

0x14 - Placeable Reference - Fragment1 contains a "magic" string: "SPRITECALLBACK". Data2 contains at least one fragment reference that refers to either a 0x2D or 0x11 fragment. I don't understand what all the parameters in this fragment mean.




There is much more to .WLD files than this, and this document doesn't contain anything on the file header and it's overall organization. Also, many fragments contain flags that have to be set properly for the zone to work (e.g. whether a 0x22 fragment refers to a 0x36 fragment). See frmMainUnit.pas in the OpenZone source to see how I create fragments when exporting zone files. My fragment variables typically start with "D"; that is, D36 for a 0x36 fragment, D21 for a 0x21 fragment, etc. By searching for something like "D36." you can see how I set the fields in a 0x36 fragment, for example. Over time we can possibly grow this to become a more comprehensive reference.
