# Neverwinter Nights: Enhanced Editionの日本語化
この日本語化はNWN:EEを日本語表示できるようにするランチャーとSEGAの1.69アップデートパッチをNWN:EE向けに配置するスクリプトのパッケージで新たな翻訳は含んでいない。  

<img width="1920" height="1080" alt="Neverwinter Nights Enhanced Edition_2026 11 16-08 06" src="https://github.com/user-attachments/assets/36fbe2cc-de40-4542-b45a-85f8fd9a2c22" />

## 必要なもの
- [NWN-EE-Japanese.7z](https://github.com/ebith/nwn-ee-japanese/releases/latest/download/NWN-EE-Japanese.7z)
- NWNJapanese1.69HotUUpdate.exe

## 使い方
- `NWN-EE-Japanese.7z`を展開し、フォルダの中に`NWNJapanese1.69HotUUpdate.exe`を置いてから`install.bat`を管理者権限で実行する
- SteamのライブラリからNWN:EEを選び、歯車アイコン => プロパティ => 一般で起動オプションに`"C:\Program Files (x86)\Steam\steamapps\common\Neverwinter Nights\bin\win32\nwnjp_loader.exe" %command%`を入力する
- プレイ

## 注意
- 対応バージョンはv89.8193.37-17
- `install.bat`の引数指定でデフォルト位置以外へもインストールできるがテストはしていない

## 本編及びDLCを快適に遊ぶためのメモ
- ビルドに迷ったら [Lilura1: BEST BUILDS Neverwinter Nights NWN, Class Solo Beginner](https://lilura1.blogspot.com/2021/01/Neverwinter-Nights-NWN-Best-Build-Class-Solo-Beginner.html)
- 仲間の操作や高速バフで便利に遊ぶなら  [Philos' Enhancing Player System (PEPS) | The Neverwinter Vault](https://neverwintervault.org/project/nwnee/script/philos-enhancing-player-system-peps)
- 鍵開けや罠解除の待ち時間を短くするにはDocuments\Neverwinter Nights\developmentにこの[ruleset.2da](https://gist.github.com/ebith/2dfeffc943e94bb174ef0e1cc344fdfe)を入れる
- セーブデータを編集したかったら [yabisiktir/nwn-save-editor](https://github.com/yabisiktir/nwn-save-editor)
