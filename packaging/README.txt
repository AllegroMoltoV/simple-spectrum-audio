Simple Spectrum / シンプルスペクトラム
====================================

Spectrum analyzer: shows the level of the input at each frequency from 20 Hz to 20 kHz, and passes the audio through unchanged. VST® 3 and CLAP formats for Windows x64 and Linux x86_64.
入力の音の周波数ごとの大きさを 20 Hz から 20 kHz の対数の軸に表示し、音は変えずに出力するプラグインです。Windows x64 と Linux x86_64 向けの VST® 3 と CLAP の形式で配布します。

Source code and verification / ソースコードと検証:
  https://github.com/AllegroMoltoV/simple-spectrum-audio

Install / 導入
--------------

1. Close your DAW. / DAW を終了します。
2. Windows: copy the folder "VST3\Simple Spectrum.vst3" to one of:
   Windows では、「VST3\Simple Spectrum.vst3」フォルダーを次のどちらかへコピーします。
     %LOCALAPPDATA%\Programs\Common\VST3\   (only you / 自分だけ)
     C:\Program Files\Common Files\VST3\     (all users, needs administrator / 全ユーザー。管理者権限が必要)
3. Windows, for CLAP hosts: copy "CLAP\Simple Spectrum.clap" to one of:
   Windows の CLAP に対応した DAW では、「CLAP\Simple Spectrum.clap」を次のどちらかへコピーします。
     %LOCALAPPDATA%\Programs\Common\CLAP\
     C:\Program Files\Common Files\CLAP\
4. Linux: copy the folder "VST3/Simple Spectrum.vst3" to ~/.vst3/ and "CLAP/Simple Spectrum.clap" to ~/.clap/.
   Linux では、「VST3/Simple Spectrum.vst3」フォルダーを ~/.vst3/ へ、「CLAP/Simple Spectrum.clap」を ~/.clap/ へコピーします。
5. Start your DAW and rescan plug-ins if needed. / DAW を起動し、必要ならプラグインを再スキャンします。

No installer and no network access. The plug-in does not send any audio or data anywhere.
インストーラーはありません。プラグインは通信しません。音声やデータを外部へ送りません。

Windows draws with Direct3D 11. Linux draws with Vulkan on X11 and needs a Vulkan driver (a GPU driver or Mesa's lavapipe).
Windows では Direct3D 11、Linux では X11 の上で Vulkan で描画します。Linux では Vulkan のドライバー (GPU のドライバー、または Mesa の lavapipe) が必要です。

Parameters / パラメーター
------------------------

All parameters change the display only. / どれも表示だけに働きます。

Resolution  2048, 4096 or 8192 points. Higher shows low frequencies in more detail and reacts slower. / 周波数の細かさ
Range       Bottom of the display: -60, -90 or -120 dB. / 縦軸の下限
Tilt        0, 3 or 4.5 dB per octave around 1 kHz. 3 dB/oct shows pink noise flat. / 1 オクターブごとの持ち上げ
Smoothing   0 to 100 %. Averaging time of the display, up to 0.6 s. / 表示の時間の平均の長さ

Licenses / ライセンス
--------------------

Simple Spectrum is released under the MIT License (LICENSE.txt).
Third-party licenses are in THIRD_PARTY_LICENSES: Visage (MIT), the fonts Lato (SIL OFL 1.1), Droid Sans Mono
(Apache-2.0) and, on Linux, Twemoji Mozilla (Apache-2.0 and CC-BY-4.0), FreeType (FTL), stb_image, the Lucide
cursors (ISC), Vulkan-Headers (Linux), PFFFT, CLAP, clap-wrapper, {fmt} and the VST 3 SDK.

VST is a registered trademark of Steinberg Media Technologies GmbH.
