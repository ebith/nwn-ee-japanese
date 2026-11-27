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

## 備考
- 対応バージョンは89.8193.37-17
- インストール時に生成される`Documents\Neverwinter Nights\nwnjp-dictionary.html`は検索可能な英日テーブルで英語の攻略情報を探したい場合などに便利
- 同じフォルダに生成される`nwnjp-movie-subtitles.txt`はムービーの日本語字幕に相当するテキスト
- `install.bat`の引数指定でデフォルト位置以外へもインストールできるがテストはしていない

## 本編及びDLCを快適に遊ぶためのメモ
- ビルドに迷ったら [Lilura1: BEST BUILDS Neverwinter Nights NWN, Class Solo Beginner](https://lilura1.blogspot.com/2021/01/Neverwinter-Nights-NWN-Best-Build-Class-Solo-Beginner.html)
- 仲間の操作や高速バフで便利に遊ぶなら  [Philos' Enhancing Player System (PEPS) | The Neverwinter Vault](https://neverwintervault.org/project/nwnee/script/philos-enhancing-player-system-peps)
- 鍵開けや罠解除の待ち時間を短くするにはDocuments\Neverwinter Nights\developmentにこの[ruleset.2da](https://gist.github.com/ebith/2dfeffc943e94bb174ef0e1cc344fdfe)を入れる
- セーブデータを編集したかったら [yabisiktir/nwn-save-editor](https://github.com/yabisiktir/nwn-save-editor)

### 注意すべき仕様及びバグ
- PEPSを入れると仲間を複数連れ歩けるようになる機能と1章エピローグ突入時の処理がかち合って変になるので、突入前に連れていきたい仲間以外から貰った契約アイテムを全て捨てておく必要がある
- 仲間の個人クエストを次の章でも続けるには前の章で受け取った報酬アイテムを主人公が持ったままにする必要がある
- SoUのグレンディルの救出クエストではグレンディルに`しばらくここにいるんだ。`と言って待たせるとクエストが進まなくなる

### Shadows of Undrentide
#### 粉砕機
| 素材 | 産物 | 回数 |
| --- | --- | --- |
| 石英 ＋ 緑の胞子 | リング・オブ・ディジーズ・イミュニティ |
| 石英 ＋ シャドウ・ハートの心臓 | レッサー・アミュレット・オブ・ヘルス|
| フェン・ベリー×2 ＋ 空のボトル | ポーション・オブ・バークスキン |
| 訓練用の剣 ＋ 石英 | ザ・ティノマン |

#### 奇妙な機械
| 攻撃用の壺に入れる素材 | 効果 |
| --- | --- |
| ルビー | 10d6火炎（範囲内の敵） |
| ルビー×3 | 10d6火炎（範囲内の敵） |
| ベラドンナ×4 | スロー |
| ドラゴンの血 | 爆発でノックダウン（3〜6ラウンド）＋2d6ダメージ |
| スラードの舌×10 | 死の魔法（12d4ヒットダイス分） |
| 妖精の粉 | 集団スタン（意志セーヴ18、6秒） |

- ルビー1個と3個は効果が同じ
- ベラドンナの攻撃効果は自分にスローがかかる

| 防御用の壺に入れる素材 | 効果 |
| --- | --- |
| ルビー | スペル・マントル |
| ルビー×3 | グレーター・スペル・マントル |
| ベラドンナ×6 | ヘイスト |
| ドラゴンの血 | グレーター・ストーンスキン |
| スラードの舌×6 | エナジー・バッファー |
| 妖精の粉 | インプルーブド・インヴィジビリティ |

