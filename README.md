# otoca-camhook

English | [日本語](README-ja.md)

Stands in for the cabinet camera of otoca d'or (NCG): printed card images are handed to the game as the camera frame.
It also fixes the 60-second wait after printing a star (hologram) card on spice2x.

The game reads the QR code on a card through `libcamera.dll` (DirectShow, physical USB cameras only).
spice2x's `-otocacamhook` only replaces the camera functions with ones that show nothing, so cards cannot be read with it.
This DLL replaces the same functions, and `LibCameraGetImage` returns the frame that `otoca-scan` puts in shared memory.

## Usage

Example placement:

- `otoca-camhook.dll`: the game's `modules` folder
- `otoca-scan.exe`: the game folder
- Printed images: a `printer` folder inside the game folder

1. Add `-k <game folder>\modules\otoca-camhook.dll` to the spice2x launch options.
   `-otocacamhook` can stay on.
   spice loads -k DLLs after its own hooks, so this DLL's replacements take effect.
2. To keep the printed images in one place, create the `printer` folder first and add spice's `-printerpath <game folder>\printer`.
   spicecfg calls it "SDVX Printer Output Path", but it applies to otoca as well.
   Images cannot be written if the folder does not exist.
   Without it, `printer_N.png` is written to the game folder.
3. Start `otoca-scan.exe` and use "フォルダ…" (Folder) to pick the folder `printer_N.png` is written to.
   The chosen folder is saved in `otoca-scan.ini` (next to the exe).
4. Select the card to scan from the list.
   The red frame shows where the QR code was found.
   The newest prints are at the top, and new prints are added automatically.
5. On the game's scan screen, keep "押している間 カードをかざす" (hold the card while pressed) pressed (or hold the space key).
   The card is in front of the camera only while it is pressed; on release the camera shows nothing again.
6. Throw away cards you no longer need with "このカードを捨てる（ごみ箱へ）" (throw this card away, to the Recycle Bin), or press Delete in the list.
   The image goes to the Recycle Bin and the next card is selected.
   Restoring it from the Recycle Bin brings it back to the list.
   spice uses the lowest free number, so the next print may be written under a number that was thrown away.
   The list is sorted by modification time, so the new print still comes first in that case.

The lower line of the status display ("ゲームのカメラ", the game's camera) shows when the game last read the camera.
If it has never been read, the DLL is not loaded.

## The star card print wait

arkkep.dll keeps two printer slots (normal cards in slot 0, star cards in slot 1), but its print-finished callback only checks slot 0.
spice2x patches arkkep so star cards are printed, but the completion never reaches slot 1, so the print screen does not move on until arkkep gives up (1800 polls, about 60 seconds).
This DLL replaces that callback and marks every slot on the same printer as finished.
spice reports a print as finished after 4 seconds, so star cards also move on after 4 seconds.
The callback is replaced only when the bytes match NCG 2019012900's arkkep.dll, and the log then shows `star print callback fixed`.

## Building

Build with `build.ps1` (Visual Studio, 32-bit).
The outputs are `build\otoca-camhook.dll`, `build\otoca-scan.exe` and `build\qrtest.exe`.

## Testing

```
build\qrtest.exe <game modules folder> <absolute path of otoca-camhook.dll> <printer_N.png>...
```

qrtest loads the game's `libcamera.dll` and `arkkep.dll`, loads this DLL on top, and checks the following.

- **Star print:** with the original arkkep, slot 1 stays busy; after the replacement, both slots are finished.
- **Cards:** the QR code is cut out of each card the same way otoca-scan does and put in shared memory.
  The frame from `LibCameraGetImage` goes through the same conversion as arkkep (`default_convert`) and is read with the game's `QRDecode.dll`.
  `OK` means arkkep accepts the QR code.
- **After release:** the frame goes back to white.
