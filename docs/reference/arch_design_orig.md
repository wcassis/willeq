# willeq architecture overview / design

Overview:

willeq is a custom EverQuest client targeting a feature set and zone list from the original game, Kunark, and Velious. willeq is designed to run on the maximum range of hardware and uses the original game assets. willeq's constrained mode settings are not optional and define absolute maximums the application may use in terms of resources. willeq's display output ranges from none (headless) to full 3D. 3D is supported through the software renderer, desktop OpenGL, and OpenGL ES2.0. Multiple display modes may be active at one time (e.g. console+3D, console+3D+RDP, etc). More display modes will be added in the future (MUD mode, 2D overhead, etc). The display outputs are determined at application start-up and should persist while the application runs.

Zoning:

willeq has two zone loading states: 1) setup, and 2) in-game.

The setup state starts with willeq's connection to the zone server. After a ClientUpdate packet is received with the player's spawn id, which indicates all setup data has been sent by the server, the zone's HCMap/S3D data must be loaded for PVS/collision detection. The data load process must always respect the constrained limits and should prioritize by PVS region, starting with the player's region and radiating outward. This is a synchronous loading process and blocks until completed. Once finished, willeq moves to in-game. The in-game state lasts until the player zones or quits.


The following applies only to 3D graphical output:

willeq has two zone loading modes for graphical output: 1) automatic, and 2) manual. Automatic zone loading progresses one at a time through the loading phases without any user interaction. Manual zone loading progresses one at a time through the loading phases when prompted by user input via a /load command (e.g. /load s3d). The graphical feature flags (e.g. enableShaders, skipEntityBuild, skipObjectBuild, skipEntityTextureUpload, etc) enabled in the constrained config determine what, if anything, is done at each phase. It's possible to have all flags disabled, all flags enabled, and any combination in-between.

The first thing that's rendered graphically when the in-game state is reached is the "instant scene". The "instant scene" is built differently depending on manual or automatic loading modes. Zero or more completed loading phases are required for the "instant scene" and gameplay to start.

In automatic loading mode, willeq steps through the loading phases one at a time, waiting at each phase until it is fully completed before moving to the next phase. This process completely blocks gameplay until it finishes. The player sees the loading screen while this proces happens. The governor green and 1-frame-per-green requirements do not apply while automatic loading is moving through the loading phases. When the last phase completes, the "instant scene" is rendered and actual gameplay starts.

In manual loading mode, willeq does not move forward with any loading phase until prompted. The "instant scene" is immediately shown with the terrain rendered using HCMap data and using placeholder cubes for all entities (no models, textures, lights, animations, etc). As the loading phases are manually triggered, assets are loaded and the scene updates. The game is fully playable during this process.

willeq's asset loading strategy is based on PVS distance from the player and ranges from a depth=0 (same PVS region) to depth=ALL (all PVS regions). This loading starts at the player's PVS region and branches to connected regions until the configured depth is met or resources are exhausted. Asset loading must always fully track memory use, fully track fps impact, stay within constrained mode limits, must respect the governor green requirement, must have all deferrable work moved to one of the non-render threads, must use the GPU update thread to upload data, and may only use the render thread for single-step operations that follow the 1-frame-of-work-per-green limit.

Once in-game, there is only one strategy for handling assets: lazy-load (assets load/unload as gameplay requires while staying within constraints). Asset loading is prioritized, starts at the player and radiates outward, grouped by type for efficient loading, and sorted by distance. The assets that willeq loads are configurable and allow for a range from none to all. 


Rendering overview: 

willeq's rendering pipeline has 3-4 simultaneous threads depending on the renderer:

1. Render: highest priority, real-time
2. GPU Update: second highest priority (only runs if GLES2 is enabled)
3. Simulation: third highest
4. Background: lowest priority

The render thread has two absolute requirements: 1) Governor GREEN, and 2) 1-frame-per-GREEN. The Governor GREEN requirement mandates that work can only be done when the governor is in a green state. The 1-frame-per-GREEN requirement mandates that only one frame of time may be consumed by work during a governor green state.

The render thread is limited to only those operations that cannot be offloaded to the GPU update, simulation, or background threads. Any GPU memory related operation must happen on the GPU update thread. All file i/o must happen on the background thread. The background thread should be prioritized for use so that the simulation, GPU update, and render threads are kept as slim as possible. The simulation thread should do one thing at a time and any blocking or long-running tasks should be moved to the background thread. The background thread should do one thing at a time and tasks should be broken down into as small steps as possible with only one step running per thread loop so no one task can monopolize the thread. The background thread is not allowed to spawn additional threads.

Current codebase issues:
1. There's a significant amount of overlap/duplication between the pre-loading and in-game loading routines. Automatic and manual modes can use the same functions but with a flag to identify the mode. The only functional difference is that the loading screen is shown during automatic mode and not during manual. Both automatic and manual loading modes should use the background thread to do work and are not allowed to spawn additional threads. 

2. Lazy-loading is for controlling two things: 1) loading rate, 2) amount loaded. The loading rate needs to be tightly controlled so that only one thing is loaded at a time. Within this rate limit, the lazy-loader will load up to the configured distance from the player (which could be the entire zone).

3. There are extra threads being created for processes that can be scoped and made synchronous. For example, the EntityPrepWorker should not have its own thread and instead should do work on the background thread. The spell icon loader is similar. I think there may be additional opportunities like this.

4. We have made many changes in this branch and there is quite a bit of dead code.




