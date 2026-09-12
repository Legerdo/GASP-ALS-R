# Mover の過去 Trajectory が上昇する問題

## 原因

`AB_Gar.Update_Trajectory` で、Mover の生成結果に重力・床衝突補正を追加し、補正済みの
`OutTrajectory` を次フレームの履歴にも使う `Trajectory` へ戻していた。
`SandboxCharacter_Mover_ABP` は、Mover の生成結果を直接保存している。

現行エンジンの `PoseSearchTrajectoryLibrary.cpp` では、履歴更新時に次の差から
移動床による変位を推定し、過去サンプル全体へ適用する。

```text
推定した床変位 = 現在の実位置 - 保存した前フレームの現在点 - 現在速度 × DeltaTime
```

`PoseSearchGenerateTransformTrajectoryWithPredictor` が保存する現在点の時刻は
`+DeltaTime`。この点も `HandleTransformTrajectoryWorldCollisionsWithGravity` の
`TimeInSeconds > 0` の対象なので、後段の重力・床補正で位置が変わる。
その値を履歴へ戻すと、実位置との差が床の上昇として毎フレーム加算される。
描画関数が高さを積み上げていたわけではない。

修正前の静止中の実測では、Mesh の実位置 Z が 2.15 cm、保存した現在点が 0.01 cm、
最古の履歴点が約 118.74 cm だった。実速度はゼロなのに
`Trj_PastVelocity.Z` は約 -45.80 cm/s になっていた。
地上移動用の主要な過去 Trajectory 評価が XY 成分中心であることは、見た目の
不具合が目立ちにくかった理由と考えられる。ただし、誤りは表示だけに限定されない。

## 修正

`AB_Gar.Update_Trajectory` の `Set Trajectory` に渡す線を、衝突補正ノードの
`OutTrajectory` から、`PoseSearchGenerateTransformTrajectoryWithPredictor` の
`OutTrajectory` へ変更した。Sample Mover と同じく、生の Mover 生成結果を保存する。

既存の衝突情報計算と `TrajectoryCollision` への代入は残した。
衝突補正ノードには、補正済み軌跡を Mover 履歴へ戻さない理由をコメントした。
履歴数・間隔、予測数・間隔、Chooser、DB、エンジンソースは変更していない。

## 検証

- ABP コンパイル: エラー 0、警告 0。
- 変更前後の監査: 1,135 ノード・1,938 接続を維持。接続先の変更以外は、ピン既定値、
  Chooser の選択データ・参照、DB のアニメーション登録・Schema が同一。
- 平地で 18 秒の PIE 計測。上限 FPS を 30 から 60 へ切り替え、停止・移動・ジャンプを実行。
  これは FPS 上限の変更であり、実測 FPS が常にその値だったという意味ではない。
- 全 150 回の観測で `NoValidAnim` は 0。空中では `In Air Transition` を観測。
- 停止中 59 回の観測で、過去点と Mesh の高さずれ、現在点と Mesh の高さずれ、
  `Trj_PastVelocity.Z` の絶対値はすべて最大 0。
- 計測後に `AB_Gar` のみ保存し、保存後の監査結果が計測時の構成と一致することを確認。
- 坂道・移動床については、この回の自動計測では未検証。

再計測用の `Tools/ue_scripts/verify_trajectory_history.py` は、平坦な GAR テストレベルで
新しい PIE を開始してから、エディタの Python で実行する。
18 秒で設定を戻して対象 PIE を終了し、`GAR_TRAJECTORY_PROBE` に計測値を出力する。
操作を自動入力するため、通常のプレイ確認と同時には実行しない。
