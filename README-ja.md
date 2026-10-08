# otoca-camhook

[English](README.md) | 日本語

otoca d'or（NCG）の筐体カメラの代わりに、印刷したカードの画像をカメラ映像としてゲームに渡す。
あわせて、spice2x でキラカード（ホロ）を印刷したあと 60 秒待たされる問題を直す。

ゲームはカードの QR を `libcamera.dll`（DirectShow、実物の USB カメラだけ）で読む。
spice2x の `-otocacamhook` はカメラ関数を「何も映さない」ものに差し替えるだけなので、これではカードを読めない。
この DLL は同じ関数を差し替え、`otoca-scan` が共有メモリに置いた映像を `LibCameraGetImage` で返す。

## 使い方

配置の例は次のとおり。

- `otoca-camhook.dll`: ゲームの `modules` フォルダ
- `otoca-scan.exe`: ゲームのフォルダ
- 印刷画像: ゲームのフォルダの中の `printer` フォルダ

1. spice2x の起動オプションに `-k <ゲームのフォルダ>\modules\otoca-camhook.dll` を足す。
   `-otocacamhook` は付けたままでよい。
   spice は -k の DLL を自分の差し替えの後に読むので、この DLL の差し替えが有効になる。
2. 印刷画像を 1 か所にまとめる場合は、先に `printer` フォルダを作り、spice の `-printerpath <ゲームのフォルダ>\printer` を足す。
   spicecfg では「SDVX Printer Output Path」という名前だが、otoca にも適用される。
   フォルダが無いと画像を書き出せない。
   指定しなければ `printer_N.png` はゲームのフォルダに書き出される。
3. `otoca-scan.exe` を起動し、「フォルダ…」で `printer_N.png` が書き出されるフォルダを選ぶ。
   選んだフォルダは `otoca-scan.ini`（exe の隣）に保存される。
4. 一覧から読ませるカードを選ぶ。
   赤枠は見つかった QR の位置を示す。
   一覧は新しい印刷ほど上に並び、印刷すると自動で増える。
5. ゲームのスキャン画面で「押している間 カードをかざす」を押し続ける（スペースキーでもよい）。
   押している間だけカメラに映り、離すと何も映らなくなる。
6. 要らなくなったカードは「このカードを捨てる（ごみ箱へ）」で捨てる（一覧で Delete キーを押してもよい）。
   画像はごみ箱へ移り、次のカードが選ばれる。
   ごみ箱から戻せば一覧にも戻る。
   spice は空いている番号から使うので、次の印刷が捨てた番号で書き出されることがある。
   一覧は更新時刻の順に並ぶので、その場合も新しい印刷が上に来る。

状態表示の下の行（「ゲームのカメラ」）には、ゲームが最後にカメラを読んだ時刻が出る。
一度も読まれていなければ、DLL が読み込まれていない。

## キラカードの印刷待ち

arkkep.dll はプリンターを 2 枠持つ（通常カードは 0 枠、キラは 1 枠）が、印刷完了のコールバックは 0 枠しか見ない。
spice2x はキラの印刷を通すように arkkep を直しているが、印刷完了が 1 枠に届かないため、印刷画面は arkkep の打ち切り（1800 回の問い合わせ、約 60 秒）まで先へ進まない。
この DLL はそのコールバックを差し替え、同じプリンターの枠をすべて完了にする。
spice の印刷完了は 4 秒後なので、キラも 4 秒で先へ進む。
差し替えるのは NCG 2019012900 の arkkep.dll と中身が一致するときだけで、そのときはログに `star print callback fixed` と出る。

## ビルド

`build.ps1` でビルドする（Visual Studio、32 ビット）。
出力は `build\otoca-camhook.dll`、`build\otoca-scan.exe`、`build\qrtest.exe`。

## 確かめ方

```
build\qrtest.exe <ゲームの modules フォルダ> <otoca-camhook.dll の絶対パス> <printer_N.png>...
```

qrtest は、ゲームの `libcamera.dll` と `arkkep.dll` を読み込んだ上に、この DLL を読み込んで次の点を確かめる。

- **キラ印刷:** 元の arkkep では 1 枠が印刷中のまま残り、差し替え後は両方の枠が完了になる。
- **カード:** 各カードから otoca-scan と同じ方法で QR を切り出し、共有メモリに置く。
  `LibCameraGetImage` の映像を arkkep と同じ変換（`default_convert`）にかけ、ゲームの `QRDecode.dll` で読む。
  `OK` と出れば arkkep が受け付ける QR。
- **離したあと:** 映像が白に戻る。
