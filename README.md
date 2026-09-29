# otoca-camhook

otoca d'or（NCG）の筐体カメラの代わり。印刷したカードの画像を、カメラ映像としてゲームに渡す。

ゲームはカードの QR を `libcamera.dll`（DirectShow、実物の USB カメラだけ）で読む。spice2x の
`-otocacamhook` はカメラ関数を「何も映さない」ものに差し替えるだけなので、カードは読めない。
この DLL は同じ関数を差し替え、`LibCameraGetImage` で `scan.bmp` を返す。

## 使い方

配置するもの（同じフォルダに置く）: `otoca-camhook.dll`、`make_scan.py`、`scan.bat`

1. spice2x の起動オプションに `-k <フォルダ>\otoca-camhook.dll` を足す。`-otocacamhook` は付けたままでよい
   （spice は -k の DLL を自分の差し替えの後に読むので、こちらが勝つ）。
2. カードを読ませるとき: 印刷された画像（spice の `printer_N.png`）を `scan.bat` にドロップする。
   QR を切り出した `scan.bmp` ができ、ゲームのスキャン画面で読まれる。
3. 読み終わったら `scan.bat` をダブルクリックして `scan.bmp` を消す（置いたままだと、次のスキャン画面でも同じカードが映る）。

`scan.bmp` は 640×480・24 ビットの BMP。`make_scan.py` は QR の位置決めの模様を探して切り出すので、
カードの種類によらない。Python と numpy・Pillow が要る。

## ビルド

`build.ps1`（Visual Studio、32 ビット）。出力は `build\otoca-camhook.dll` と `build\qrtest.exe`。

## 確かめ方

`build\scan.bmp` を置いて

```
build\qrtest.exe <ゲームの modules フォルダ> build\otoca-camhook.dll
```

ゲームの `libcamera.dll` を読み込んだ上にこの DLL を読み込み、`LibCameraGetImage` の映像を arkkep と同じ変換
（`default_convert`）にかけて、ゲームの `QRDecode.dll` で読む。`OK` なら arkkep が受け付ける QR。
