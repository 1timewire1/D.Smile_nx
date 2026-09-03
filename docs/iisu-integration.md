# Launching D.Smile from iiSU (and other frontends)

## What to add to your iiSU emuladores.json

Add this as a new entry within the VTech V.Smile console block. 

```json
{
  "shortName": "vsmile",
  "longName": "VTech V.Smile",
  "releaseYear": "2004",
  "releaseDate": "2004",
  "manufacturer": "VTech",
  "retroAchievementsId": "NA",
  "romExtensions": [
    ".bin",
    ".BIN"
  ],
  "emulators": [
    {
      "id": "DSMILE",
      "name": "D.Smile (Standalone)",
      "routeType": "path",
      "packages": [
        "com.dsmile.emulator"
      ],
      "commands": [
        {
          "description": "D.Smile",
          "command": "%PACKAGE%/.ui.EmuActivity -a com.dsmile.emulator.LAUNCH_GAME -e rom %ROM% --activity-clear-top"
        }
      ]
    }
  ]
}
```

**Important:** because `routeType: "path"` hands D.Smile a raw file path, open
D.Smile once → Settings → "All files access" and grant it. Without that,
Android blocks reading ROMs outside the app's own folders on Android 11+.

## The intent contract (for any frontend)

`EmuActivity` (`com.dsmile.emulator/.ui.EmuActivity`, exported) accepts:

| Mechanism | Details |
|---|---|
| Action | `com.dsmile.emulator.LAUNCH_GAME` or `android.intent.action.VIEW` |
| Data Uri | `content://…` (with read grant) or `file://…` |
| String extras | `rom`, `ROM`, `path`, or `uri` — a file path or uri string |
| Optional extras | `pal` (boolean) — force PAL timing |

Equivalent adb test command:

```
adb shell am start -n com.dsmile.emulator/.ui.EmuActivity \
  -a com.dsmile.emulator.LAUNCH_GAME \
  -e rom "/storage/emulated/0/roms/vsmile/Alphabet Park Adventure (USA).bin"
```

Daijisho / ES-DE style launches (content Uri + ACTION_VIEW) work with the same
activity, no extra configuration.