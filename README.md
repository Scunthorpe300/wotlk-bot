# wotlk bot

* Works on linux, through lutris and proton
* Could work on bottles with minimal changes

Core Functionality

    Out-of-Process Architecture: Reads and writes game memory externally without injection

        No DLL injection
        No function detouring
        No hooking
        Only writes to addresses the client naturally updates
        Evades default Warden anti-cheat detection
        Evades Warmane's Warden RCE
        Evades TrinityCore custom client extensions.dll cheat detection

Object Management

    Player Tracking: Monitor all nearby players using minimap data
    Object Tracking:
        Track all nearby game objects efficiently
        Tracks hostile, friendly and neutral players on the minimap
        Intelligent filtering for funserver objects
        Prevents minimap spam from useless container objects
    Object Manipulation:
        Dump all GameObjects
        Dump Player Objects
        Dump Unit Objects
        Clientside flag manipulation for objects
        Lots of other fun things are possible, a lot of my experiments ended up causing silly things to happen.

Automation

    Rotation Bot: Automated combat rotation execution
    Heal Bot: Intelligent healing automation for group/raid content
    Fishing bot: Fishing!
    
Servers tested on
    Warmane, Whitemane
    Ascension (Bronzebeard),
    
    Should be compatible with all 3.3.5a 123450 clients and mods.
    
Compiling

made with clion, left profiles in for release/debug builds but command line cmake should work fine too
needs fmtlib, glfw, sfml, xdotool, and arraydatabase (in my profile)
works best on gcc with x11, but has been tested on llvm



While not entirely complete, this is as far as I want to go with it. Source code posted in hopes that (you) enjoy it as much as I have.
Make pull requests if you want, good stuff ill merge sure!
    Also: Sandbox your proton whenever running wow private servers they can run anything on your computer, protect yourself!
    

