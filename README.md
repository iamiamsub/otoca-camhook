# otoca-camhook

otoca d'or（NCG）の筐体カメラの代わり。印刷したカードの画像を、カメラ映像としてゲームに渡す。
あわせて、spice2x でキラカード（ホロ）を印刷したあと 60 秒待たされる件を直す。

ゲームはカードの QR を `libcamera.dll`（DirectShow、実物の USB カメラだけ）で読む。spice2x の
`-otocacamhook` はカメラ関数を「何も映さない」ものに差し替えるだけなので、カードは読めない。
この DLL は同じ関数を差し替え、`otoca-scan` が共有メモリに置いた映像を `LibCameraGetImage` で返す。

## 使い方

配置の例: `otoca-camhook.dll` はゲームの `modules` フォルダ、`otoca-scan.exe` はゲームのフォルダ、
印刷画像はゲームのフォルダの `printer` フォルダ。

1. spice2x の起動オプションに `-k <ゲームのフォルダ>\modules\otoca-camhook.dll` を足す。`-otocacamhook` は
   付けたままでよい（spice は -k の DLL を自分の差し替えの後に読むので、こちらが勝つ）。
2. 印刷画像をまとめるなら、先に `printer` フォルダを作り、spice の `-printerpath <ゲームのフォルダ>\printer` を足す
   （spicecfg では「SDVX Printer Output Path」。otoca にも効く。フォルダが無いと書き出せない）。
   指定しなければ `printer_N.png` はゲームのフォルダに書き出される。
3. `otoca-scan.exe` を起動し、「フォルダ…」で `printer_N.png` が書き出されるフォルダを選ぶ。
   選んだフォルダは `otoca-scan.ini`（exe の隣）に残る。
4. 一覧から読ませるカードを選ぶ。赤枠が見つかった QR。一覧は新しい印刷が上で、印刷すると自動で増える。
5. ゲームのスキャン画面で「押している間 カードをかざす」を押し続ける（スペースキーでもよい）。
   押している間だけカメラに映り、離すと何も映らなくなる。
6. 要らなくなったカードは「このカードを捨てる（ごみ箱へ）」（一覧で Delete キーでもよい）。画像をごみ箱へ移し、
   次のカードを選ぶ。ごみ箱から戻せば一覧に戻る。spice は空いた番号から使うので、次の印刷は捨てた番号で
   書き出されることがある（一覧は更新時刻の順なので、それでも上に来る）。

下の行の「ゲームのカメラ」は、ゲームが最後にカメラを読んだ時刻。一度も読まれていなければ DLL が読み込まれていない。

## キラカードの印刷待ち

arkkep.dll はプリンターを 2 枠持ち（通常カードは 0、キラは 1）、印刷完了のコールバックが 0 枠しか見ない。
spice2x はキラの印刷を通すように arkkep を直しているが、完了は 1 枠に届かないので、印刷画面は arkkep の
打ち切り（1800 回の問い合わせ ≒ 60 秒）まで進まない。この DLL はコールバックを、同じプリンターの枠を
すべて完了にするものに差し替える（spice の印刷完了は 4 秒後なので、キラも 4 秒で進む）。
NCG 2019012900 の arkkep.dll と中身が一致するときだけ差し替え、ログに `star print callback fixed` と出る。

## ビルド

`build.ps1`（Visual Studio、32 ビット）。出力は `build\otoca-camhook.dll`、`build\otoca-scan.exe`、`build\qrtest.exe`。

## 確かめ方

```
build\qrtest.exe <ゲームの modules フォルダ> <otoca-camhook.dll の絶対パス> <printer_N.png>...
```

ゲームの `libcamera.dll` と `arkkep.dll` を読み込んだ上にこの DLL を読み込む。

- **キラ印刷:** 元の arkkep では 1 枠が印刷中のまま残り、差し替え後は両方が完了になることを確かめる。
- **カード:** 各カードは otoca-scan と同じ方法で QR を切り出して共有メモリに置く。`LibCameraGetImage` の映像を
  arkkep と同じ変換（`default_convert`）にかけ、ゲームの `QRDecode.dll` で読む。`OK` なら arkkep が受け付ける QR。
- **離したあと:** 映像が白に戻ることも確かめる。
