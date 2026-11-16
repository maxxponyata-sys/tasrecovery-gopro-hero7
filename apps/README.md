# TasRecovery apps

An app is a folder with an `app.conf`:

```
name=System info        # shown in the App manager
exec=run.sh             # started with "sh <exec>" inside the app folder
desc=short description
fullscreen=0            # 1 = the app gets the display and input; tasui gives them back afterwards
```

Copy app folders to the SD card as `SD:/tasapps/<id>/`. In TasRecovery open **App manager**:
apps on the SD card can be run directly or installed to the eMMC (`userdata/tasapps/<id>/`).
Console apps (`fullscreen=0`) have their output shown in the viewer when they finish.
Full-screen apps can draw with `tasfb` (320x480 RGBA frames on stdin). `$TAS_APPDIR` is the app folder.
