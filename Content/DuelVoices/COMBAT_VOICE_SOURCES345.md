# 北塔 3.4.5 战斗录音来源

当前运行库含 18 个事件槽和 5 个 Boss 候补，共 23 段独立录音、35.63 秒。沿用 3.4.4 已核实的原作逐句索引版本，新增 8 段原作录音，替换希罗旧战技嘶喊，并为希罗、艾玛补齐自身落败事件。

| 角色与事件 | 运行文件 | 原作编号与台词 |
| --- | --- | --- |
| 希罗战技主句 | North_Hiro_Art_Calm.wav | 0201Adv26_Hiro008「你在说谎。」；相邻 Hiro009 是感叹号版，本次未采用 |
| 希罗战技候补 | North_Hiro_Art_Resolve.wav | 0206Trial04_Hiro043「不会让你为所欲为……」 |
| 希罗自身落败 | North_Hiro_Loss.wav | 0201Adv20_Hiro022「……是我的错。」 |
| 艾玛战技候补 | North_Ema_Art_Stop.wav | 0203Adv10_Ema002「快、快住手……！」 |
| 艾玛魔女化候补 | North_Ema_Witch_Enough.wav | 0202Adv15_Ema011「你说够了吗。」 |
| 艾玛自身落败 | North_Ema_Loss.wav | 0201Adv24_Ema005「……怎么会……」 |
| 小雪战技候补 | North_Yuki_Art_Sentence.wav | 0206Trial02_Yuki008「判处——死刑。」 |
| 小雪开场候补 | North_Yuki_Intro_Execution.wav | 0206Trial01_Yuki003「……处决全人类。」 |

Boss 的同一事件在成功播放后按主句、候补轮换；被对白保护、冷却或同一事件去重拦下时不推进候补。玩家保留主句。Defeat 仍只对应玩家倒下，Loss 只对应该 Boss 自身落败。艾玛呼叫「希罗酱」仍限于面对希罗的开场。既有优先级、说话时压低音乐与语音静音门控保留。

旧 North_Hiro_Art.wav（0203Adv14_Hiro016「我不会放弃你！」，PCM CRC32 46145fb8）已从当前运行表、候补与当前清单排除。新文件名及校验值也阻止旧用户目录文件覆盖新句。COMBAT_VOICE_PROVENANCE344.json 只保留历史记录；当前验证使用 COMBAT_VOICE_PROVENANCE345.json。

新增原始 OGG 保存在 Originals345。当前清单记录每条的 URL、说话人、中文原文、原作编号、原文件与输出 SHA-256、PCM CRC32、裁切区间及本地日语 ASR 原始结果。索引 act02.json 的 Git blob 为 067d8d1d5417f062833c953069121ff8f31dd8ea，voice-full.json 为 7c87847a67353bf521e6c2d31025e1334deaf1cf；本次先核对缓存字节与这两个版本一致，再取得对应录音。所有新文件保持原声，转为单声道 24 kHz PCM16，首尾近静音保留 30/70 ms 缓冲，未合成或转换音色。

8 条新增录音均运行本地 faster-whisper base 日语识别。短句有同音字误差，如「嘘だ」「判決は死刑」未被机器准确写出全部汉字；原始机器结果完整保留，角色和中文台词以编号对应的说话人与逐句索引交叉核对。该步骤不等同于人工试听或引擎混音验收。

来源类别与原权利人说明沿用 [3.4.4 来源记录](COMBAT_VOICE_SOURCES344.md)：录音由[大魔女图书馆](https://github.com/QwQSakuya/Manosaba-Library)的非官方原作索引定位；网页代码许可不覆盖原作录音。游戏录音版权仍归 Re,AER / Acacia 等原权利人，WePlay 现场 Kiang 另行署名。

运行 `python -X utf8 Content/DuelVoices/verify_combat_sources.py` 验证当前 23 段录音。引擎回归为 `ManosabaDuel.NorthTower.AudioRouting`；加 `-DuelCheckAudioComponents` 时实际创建音频组件，核对 5 个事件主句/候补轮换、字幕、重复事件与玩家隔离。编译、引擎运行和混音验收由统一验收阶段执行。
