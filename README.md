This project uses an event oriented strucure where an event stack is actioned constantly. The update method handles the rendering done by raylib along side registering 
whatever imputs the player has done among other things.
The rendering is layered allowing multiple objects to be stacked on top of each other on screen allowing the illusion of three dimensions (eg. someone being behind a tree)
The engine includes a rudimentary sound manager, allowing certain rooms to have certain sounds 

WARNING: THIS PROJECT IS BUILT AROUND THE TILED TOOL. IT READS JSON FILES DETAILING THE PARAMETERS AND LOCATIONS OF TILE SETS.

The combat state works by pushing the event stack. As is by standard. One could implement menu and dialogue instances similarly.
The features a tile can have are detailed in the tilemap source code with the suggestive titles 
Details pertaining music and audio are mostly located in the overworld manager class, although the sounds the player makes when walking on certain surfaces is a trait owned by tiles
and thus is specified in the tilemap file
The details pertaining to enemies are detailed in the CombatState. One could and probably should add an enemy tile type with a specific look and or texture.
  
