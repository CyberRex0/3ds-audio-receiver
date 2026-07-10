# 3DS Audio Receiver APP
## 概要
Wi-Fi経由でUDPにてPCM音声を受け取って、3DSでリアルタイムに再生するアプリ。

ユーザーからは3DSのDACで再生可能なサンプリング周波数、量子化ビット数でUDPを使い、PCMで送ってもらう。

アプリの画面にIP、ポート、使い方を表示する。

# 使用技術
C言語

# 注意
- 言語の特性上、メモリの管理問題が起きやすいので、管理に注意する。

- エラーハンドリングが適切でないとフリーズするので、ハンドリングをする。

# 開発ポイント

- `ax` スキルを活用する。
- devkitProは `C:\devkitPro` にインストールしてあるので、必要なパッケージのインストール、アプリのビルドなどはここで行う。

開発資料:

- libctru
    https://libctru.devkitpro.org/
    
    アプリケーションの要

- citro2d
    https://citro2d.devkitpro.org/

    グラフィックス系

- サンプルプログラム集
    C:\devkitPro\examples\3ds
