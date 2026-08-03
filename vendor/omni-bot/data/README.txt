Omni-bot for Enemy Territory is used to add computer-controlled players to the game. 

- Install Enemy Territory (ET)
You can download the latest version of ET from https://www.etlegacy.com/
It is recommended to have 2 separate ET installations, each in its own folder - the first for playing online a the second to run your server with Omni-Bot. Many public servers have custom sounds, textures and menus which can completely change or brake the game. All these extra pk3 files will be downloaded only to the first ET installation and your second installation will remain clean and usable for Omni-bot.


- Install MODs
Omni-Bot does not work with vanilla etmain. You need some mod. You can use our omnibot mod or you can choose some 3rd party mods. It's possible to have multiple mods installed. There are many mods that support Omni-bot: Bastardmod, ETBlight, ETNam, ETPub, infected, Jaymod, legacy, N!tmod, NoQuarter, silEnT.


- Install Omni-Bot
Note: If you have ET:Legacy, you will find Omni-Bot already installed in the legacy folder.
There is omni-bot folder inside the ZIP archive. Extract it. Omni-Bot can be installed to any location. It is not necessary to have omni-bot inside Enemy Territory folder. It is recommended to install it into folder which has permission to write files. 


- Create config file
Many mods already have a config file (for example jaymod.cfg, silent.cfg, nitmod.cfg). They already have the cvars in them so you just need to edit them. If your mod does not have any config file yet, create a new file (for example server.cfg) and save it to mod's folder.
The server config CVAR's you need to add/edit are as follows

set omnibot_enable "1"
set omnibot_path = "C:\Your install folder\omni-bot"

The omnibot_path needs to be set to the absolute path of the omni-bot folder.
Be aware Windows uses left-slashes '\' and Linux/UNIX based-systems use right-slashes '/' as separator-char.

- Optional Flags
This is an optional cvar, you only need to add it if you want to disable certain bot functions.

set omnibot_flags "0"

Add the following flags if required
1 Disable XPSave for bots
2 Bots cannot mount tanks
4 Bots cannot mount mg42
8 Don't count bots (this affects the value of the cvar 'omnibot_playing' which contains the number of bots playing or -1 if this flag is set)
Note: some mods may have other flags

- Optional Log Size
Log files are written to folder omni-bot/et/logs. If the folder does not exists, log files are written to Enemy Territory game folder. File name is omnibot_<mapname>.log.

set omnibot_logsize "0"

Default value 0 disables logging. Value greater than 0 means maximal file size in KB and also allows appending more matches to a single file.


- Run ET
You can start ET from command line, but it's recommended to create a shortcut and put that shortcut to the start menu or desktop.
On local home computer create shortcut to ET.exe or etl.exe on Windows, et or etl on Linux.
On dedicated server use ETDED.exe or etlded.exe on Windows, etded or etlded on Linux.
Right click & select properties to add parameters:

+set fs_game mod_name +set com_hunkmegs 128 +exec server.cfg

Replace the word mod_name with the mod you are using. It must be exactly mod's folder name.
Replace server.cfg with your config file name.
Note: If you start ET without fs_game parameter, then you can choose MOD from the menu.


- Load map
There are a few different ways to load a map:
1) Click on HOST GAME in the menu and then choose map name from a list.
2) Open the game console (press ~ key between Esc and Tab) and type
/map <mapname>
If you want to enable cheats, you must type
/devmap <mapname>
If you don't know <mapname>, you can open map's PK3 file in any ZIP application and look into the folder "maps".
3) Add map <mapname> command to the server config.


- Add bots
you can configure number of bots to join every game automatically
/bot maxbots 16
or you can add bots manually
/bot maxbots -1
/bot ab 15


- Change difficulty
(0=poorest,1=very poor,2=poor,3=easy frag,4=standard,5=professional,6=uber)
/bot difficulty 6
