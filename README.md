# otoca-camhook

otoca d'or（NCG）の筐体カメラの代わり。印刷したカードの画像を、カメラ映像としてゲームに渡す。

ゲームはカードの QR を `libcamera.dll`（DirectShow、実物の USB カメラだけ）で読む。spice2x の
`-otocacamhook` はカメラ関数を「何も映さない」ものに差し替えるだけなので、カードは読めない。
この DLL は同じ関数を差し替え、`otoca-scan` が共有メモリに置いた映像を `LibCameraGetImage` で返す。

## 使い方

配置するもの（同じフォルダに置く）: `otoca-camhook.dll`、`otoca-scan.exe`

1. spice2x の起動オプションに `-k <フォルダ>\otoca-camhook.dll` を足す。`-otocacamhook` は付けたままでよい
   （spice は -k の DLL を自分の差し替えの後に読むので、こちらが勝つ）。
2. `otoca-scan.exe` を起動し、「フォルダ…」でゲームのフォルダ（spice が `printer_N.png` を書き出す所）を選ぶ。
   選んだフォルダは `otoca-scan.ini`（exe の隣）に残る。
3. 一覧から読ませるカードを選ぶ。赤枠が見つかった QR。一覧は新しい印刷が上で、印刷すると自動で増える。
4. ゲームのスキャン画面で「押している間 カードをかざす」を押し続ける（スペースキーでもよい）。
   押している間だけカメラに映り、離すと何も映らなくなる。

下の行の「ゲームのカメラ」は、ゲームが最後にカメラを読んだ時刻。一度も読まれていなければ DLL が読み込まれていない。

## ビルド

`build.ps1`（Visual Studio、32 ビット）。出力は `build\otoca-camhook.dll`、`build\otoca-scan.exe`、`build\qrtest.exe`。

## 確かめ方

```
build\qrtest.exe <ゲームの modules フォルダ> <otoca-camhook.dll の絶対パス> <printer_N.png>...
```

ゲームの `libcamera.dll` を読み込んだ上にこの DLL を読み込む。

- **カード:** 各カードは otoca-scan と同じ方法で QR を切り出して共有メモリに置く。`LibCameraGetImage` の映像を
  arkkep と同じ変換（`default_convert`）にかけ、ゲームの `QRDecode.dll` で読む。`OK` なら arkkep が受け付ける QR。
- **離したあと:** 映像が白に戻ることも確かめる。
