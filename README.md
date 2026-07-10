# 3DS Audio Receiver

Wi-Fi経由のRaw UDPデータを受信し、Nintendo 3DSでリアルタイム再生するHomebrewアプリです。

## 音声・UDP仕様

- 32728 Hz
- signed 16-bit little-endian PCM
- stereo interleaved（左16-bit、右16-bit）
- UDPペイロードは最大1400バイト、4バイトの倍数
- 既定ポートは5000

パケットヘッダー、認証、暗号化、再送制御はありません。同じLAN内で使用してください。Raw UDPのため、パケットの欠落、重複、順序逆転は受信側で判別できません。

## ビルド

ビルド済みバイナリは [Releases](https://github.com/CyberRex0/3ds-audio-receiver/releases) からダウンロードできます。

devkitProの3DS開発環境、libctru、citro2dが必要です。devkitPro MSYS2シェルで次を実行します。

```sh
make
```

生成された`3ds-audio-receiver.3dsx`と`3ds-audio-receiver.smdh`をSDカードの`/3ds/3ds-audio-receiver/`へコピーしてください。

## 使い方

1. 3DSを送信PCと同じWi-Fiへ接続し、アプリを起動します。
2. 上画面に表示されたIPアドレスとUDPポートを確認します。
3. PC側で送信ツールを起動します。

```sh
python tools/send_audio.py audio.wav 192.168.1.25
python tools/send_audio.py audio.wav 192.168.1.25 --port 6000 --loop
python tools/send_audio.py audio.pcm 192.168.1.25 --raw
```

標準入力のRaw PCMも送信できます。

```sh
some_pcm_source | python tools/send_audio.py - 192.168.1.25 --raw
```

入力WAVが別形式の場合は、FFmpegなどで事前変換します。

```sh
ffmpeg -i input.mp3 -ar 32728 -ac 2 -c:a pcm_s16le audio.wav
```

### FFmpegから直接送信する

Python送信ツールを介さず、FFmpegで入力ファイルを変換しながら3DSへ直接送信できます。`192.168.1.25`と`5000`は、3DS上画面に表示されたIPアドレスとポートへ置き換えてください。

```sh
ffmpeg -re -i input.mp3 -vn -ac 2 -af "aresample=32728,asetnsamples=n=350:p=1" -c:a pcm_s16le -f s16le "udp://192.168.1.25:5000?pkt_size=1400&bitrate=1047296&burst_bits=11200"
```

ファイルを繰り返し再生する場合は、入力より前に`-stream_loop -1`を追加します。

```sh
ffmpeg -stream_loop -1 -re -i input.mp3 -vn -ac 2 -af "aresample=32728,asetnsamples=n=350:p=1" -c:a pcm_s16le -f s16le "udp://192.168.1.25:5000?pkt_size=1400&bitrate=1047296&burst_bits=11200"
```

`-re`で入力処理の先行とFFmpeg内部キューの増大を防ぎます。`aresample=32728`でサンプルレートを変換し、`asetnsamples=n=350:p=1`で350 stereo frames、つまり1400バイト単位に揃えます。UDP URLの`bitrate=1047296`は`32728 × 4 bytes × 8 bits`の実時間速度、`burst_bits=11200`は1400バイト1パケット分だけバーストを許可する指定です。`-re`、フィルター、UDP URLオプションは組み合わせて使用し、省略しないでください。

Windows Defender Firewallなどを使用している場合は、送信に使うPythonまたはFFmpegのネットワーク通信と、使用するUDPポートをLAN向けに許可してください。

### Windowsの音声入力をリアルタイム送信する

Windowsでは、WASAPI入力デバイス（マイクやライン入力）の音声をキャプチャして、そのまま3DSへ送信できます。最初にプロジェクトルートで依存パッケージを同期します。

```powershell
uv sync
```

利用可能なWASAPI入力デバイスを表示します。`*`は既定の入力デバイスです。

```powershell
uv run python tools/send_input.py --list-devices
```

既定の入力デバイスから送信する場合は、3DSのIPアドレスを指定して起動します。

```powershell
uv run python tools/send_input.py 192.168.1.25
```

デバイス一覧に表示されたID、または一意になる名前の一部で入力デバイスを選択できます。

```powershell
uv run python tools/send_input.py 192.168.1.25 --device 3
uv run python tools/send_input.py 192.168.1.25 --device "Microphone" --port 6000
```

このツールはWindows専用です。WASAPI共有モードでデバイスの既定サンプルレートをキャプチャし、32728 Hz、signed PCM16LE、stereoへリアルタイム変換します。mono入力は左右へ複製されます。停止するには`Ctrl+C`を押してください。キャプチャ中に音切れが起きる場合は、終了時の`dropped capture frames`と途中のWASAPI警告を確認してください。

## 3DSでの操作

- `↑/↓`: 設定項目を選択
- `←/→`: 値を変更
- `L/R`: ポートを100単位で変更
- `SELECT`: 表示言語を日本語・英語で即時切替
- `A`: 設定をSDカードへ保存して適用
- `X`: 現在の送信元を切断
- `Y`: 診断ログの記録を有効化・無効化
- `START`: 終了

設定は`sdmc:/3ds/3ds-audio-receiver/config.ini`に保存されます。言語は下画面の「言語 / Language」を選んで`←/→`でも変更でき、`A`でほかの設定と一緒に保存されます。バッファは40～250 ms、音量は0～100%の範囲で変更できます。診断ログは起動時には無効で、この設定は保存されません。

## English UI

Press `SELECT` at any time to switch the on-screen interface between Japanese and English. You can also select `Language / 言語` on the bottom screen and use `←/→`. Press `A` to save the selected language to the SD card together with the receiver settings.

## トラブルシューティング

- 音が出ない: PCと3DSが同じLANにいること、画面のIP・ポート、ファイアウォールを確認してください。
- 「不正」カウンターが増える: 送信データグラムを1400バイト以下かつ4バイトの倍数にしてください。
- 音切れする: バッファ値を増やし、Wi-Fiアクセスポイントに近づけてください。
- 遅延が大きい: バッファ値を下げてください。下げすぎると音切れしやすくなります。
- 別PCから送れない: `X`を押すか、現在の送信を2秒以上停止して送信元固定を解除してください。

### 実機診断ログ

診断ログはオプトインです。アプリ起動後に`Y`を押して明示的に有効化した場合だけ、実機上の受信・再生統計をメモリへ250 ms間隔で記録します。SDカードへの書き込みが音声再生へ影響しないよう、ログファイルは診断ログが有効な状態で`START`を押して終了したときにまとめて保存されます。再度`Y`を押して無効化すると、そのセッションで収集した未保存のログは破棄されます。

再生速度やバッファ消費の問題を調査する場合は、次の手順でログを取得してください。

1. 最新の`.3dsx`でアプリを起動します。
2. `Y`を押し、画面に「診断ログ: 有効」と表示されることを確認します。
3. 問題が分かる音声を15～30秒程度送信します。
4. 送信を停止し、診断ログを有効にしたまま3DSで`START`を押してアプリを正常終了します。
5. SDカードの`/3ds/3ds-audio-receiver/debug.log`をPCへコピーします。
6. `debug.log`、実行した送信コマンド、入力ファイルの形式を開発者へ渡します。

ログにはUDP受信バイト数、NDSPへ渡したバイト数、リングバッファ残量、NDSPの設定レート・再生位置、wave buffer状態、アンダーラン数が含まれます。最大約8分30秒分を保持し、それを超えた場合は古い記録から上書きします。

## テスト

Python送信ツールのテストは次で実行します。

```sh
python -m unittest discover -s tests -p "test_*.py"
```

リングバッファは`tests/test_ring_buffer.c`でも境界折り返しとオーバーフローを検証できます。
