# TinyCFG Benchmark Analysis

*Generated: 2026-07-21 14:44*

## Experimental setup

| Parameter | Value |
| --- | --- |
| Grammar | smart_world v3 patterns=74 |
| Platform | host-pc |
| Iterations / case | 200 |
| Parser instance size | 7684 bytes |
| Dataset size | 500 |
| Overall accuracy | 82.6% |
| Mean latency | 42.3 µs |
| P95 latency | 54.3 µs |
| Date generated | 2026-07-21 14:44 |

## Table 1 — Dataset composition by category

| Category | Cases | Positive | Negative |
| --- | --- | --- | --- |
| single clean | 55 | 55 | 0 |
| multi and | 45 | 45 | 0 |
| multi or | 25 | 25 | 0 |
| multi juxta | 35 | 35 | 0 |
| named slot | 55 | 55 | 0 |
| stt noise | 80 | 80 | 0 |
| glued dgs | 45 | 45 | 0 |
| fuzzy per | 45 | 45 | 0 |
| temporal then | 35 | 35 | 0 |
| cross domain | 35 | 35 | 0 |
| negative | 45 | 0 | 45 |
| **Total** | 500 | 455 | 45 |

## Table 2 — Parsing accuracy by category

| Category | Cases | Accuracy | Mismatches |
| --- | --- | --- | --- |
| single clean | 55 | 85.5% | 8 |
| multi and | 45 | 93.3% | 3 |
| multi or | 25 | 100.0% | 0 |
| multi juxta | 35 | 100.0% | 0 |
| named slot | 55 | 83.6% | 9 |
| stt noise | 80 | 73.8% | 21 |
| glued dgs | 45 | 88.9% | 5 |
| fuzzy per | 45 | 77.8% | 10 |
| temporal then | 35 | 82.9% | 6 |
| cross domain | 35 | 91.4% | 3 |
| negative | 45 | 51.1% | 22 |
| **Overall** | 500 | **82.6%** |  |

## Table 3 — Parse latency by category (microseconds)

Host-side timing wraps full `TinyCFG::parse()` including tokenization, pattern matching, CDA, and data-flow snapshot build.

| Category | Mean µs | Min µs | Max µs | P95 µs |
| --- | --- | --- | --- | --- |
| single clean | 22.7 | 0.0 | 1558.0 | 0.0 |
| multi and | 50.4 | 0.0 | 1582.0 | 0.0 |
| multi or | 42.8 | 0.0 | 1550.0 | 0.0 |
| multi juxta | 39.5 | 0.0 | 1598.0 | 0.0 |
| named slot | 32.8 | 0.0 | 1559.0 | 0.0 |
| stt noise | 63.4 | 0.0 | 1581.0 | 245.0 |
| glued dgs | 26.9 | 0.0 | 1549.0 | 0.0 |
| fuzzy per | 33.9 | 0.0 | 1554.0 | 33.5 |
| temporal then | 54.3 | 0.0 | 1549.0 | 86.2 |
| cross domain | 57.0 | 0.0 | 1560.0 | 43.1 |
| negative | 37.8 | 0.0 | 1573.0 | 33.5 |
| **Overall** | **42.3** | 0.0 | 1598.0 | **54.3** |

## Table 4 — Robustness metrics (positive test cases)

| Category | Mean conf. | Mean tokens | PER hits | DGS segs | Noise skip |
| --- | --- | --- | --- | --- | --- |
| single clean | 0.934 | 2.7 | 3 | 0 | 3 |
| multi and | 1.000 | 6.3 | 0 | 0 | 0 |
| multi or | 1.000 | 5.3 | 0 | 0 | 0 |
| multi juxta | 1.000 | 4.3 | 0 | 0 | 0 |
| named slot | 0.714 | 4.1 | 14 | 0 | 27 |
| stt noise | 0.263 | 8.3 | 70 | 0 | 263 |
| glued dgs | 0.964 | 2.6 | 1 | 18 | 1 |
| fuzzy per | 0.633 | 4.2 | 28 | 0 | 39 |
| temporal then | 0.981 | 6.8 | 1 | 0 | 3 |
| cross domain | 0.994 | 7.4 | 0 | 0 | 1 |
| **Positive cases** | **0.786** | 5.3 | 117 | 18 | 337 |

## Table 5 — Misclassified / failed cases

| ID | Category | Input | Expected | Actual |
| --- | --- | --- | --- | --- |
| 5 | stt_noise | OKAY UM RECORD LIVING CAMERA RIGHT NOW ER ADJFIJA … | PASS | FAIL |
| 6 | stt_noise | xxx adjfija could you hmm record bathroom camera n… | PASS | FAIL |
| 32 | stt_noise | i want to 999 scene party 09309582903 hmm please t… | PASS | FAIL |
| 39 | negative | what is the time | FAIL | PASS |
| 53 | multi_and | turn on living room light and come here | PASS | FAIL |
| 55 | fuzzy_per | okay paly lisa playlist please now | PASS | FAIL |
| 57 | fuzzy_per | could you previous song | PASS | FAIL |
| 73 | negative | what is the time please now | FAIL | PASS |
| 75 | fuzzy_per | okay unlock mike door please | PASS | FAIL |
| 82 | named_slot | unlock kate door | PASS | FAIL |
| 90 | named_slot | unlock zoe door | PASS | FAIL |
| 98 | single_clean | turn on mary living light | PASS | FAIL |
| 120 | stt_noise | Er I Want To Come Here Right 09309582903 Now So | PASS | FAIL |
| 127 | cross_domain | set timer for five minutes then turn off room fan | PASS | FAIL |
| 132 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 133 | stt_noise | okay um er scene party now this thanks | PASS | FAIL |
| 141 | negative | play football | FAIL | PASS |
| 144 | fuzzy_per | i want to set tmer for five minutes right now than… | PASS | FAIL |
| 147 | named_slot | play kate playlist | PASS | FAIL |
| 151 | single_clean | set thermostat to seventy | PASS | FAIL |
| 158 | fuzzy_per | i need to tun on office fans please | PASS | FAIL |
| 163 | glued_dgs | setThermostatToSeventy | PASS | FAIL |
| 181 | negative | turn on the sun | FAIL | PASS |
| 182 | stt_noise | could you set timer for 42 five minutes 999 okay t… | PASS | FAIL |
| 185 | negative | what is the time | FAIL | PASS |
| 186 | single_clean | record room camera | PASS | FAIL |
| 188 | stt_noise | er i the 09309582903 want to dock now adjfija $$$ | PASS | FAIL |
| 192 | stt_noise | um set timer for five minutes right the now please… | PASS | FAIL |
| 201 | negative | hey what is the time | FAIL | PASS |
| 212 | single_clean | set timer for five minutes | PASS | FAIL |
| 213 | stt_noise | a i want to cool down this now please | PASS | FAIL |
| 214 | fuzzy_per | i want to sene party okay thanks | PASS | FAIL |
| 217 | named_slot | play lisa playlist | PASS | FAIL |
| 219 | stt_noise | hmm i want to turn on 42 sarah living fan right no… | PASS | FAIL |
| 223 | negative | go to mars | FAIL | PASS |
| 225 | stt_noise | i 999 want to turn on marias adjfija back fan plea… | PASS | FAIL |
| 228 | stt_noise | THIS UM WELL $$$ LOCK UHH SO BATHROOM DOOR | PASS | FAIL |
| 236 | cross_domain | set timer for five minutes and turn off room fan | PASS | FAIL |
| 242 | stt_noise | 09309582903 i $$$ want this to patrol living now | PASS | FAIL |
| 244 | negative | what is the time | FAIL | PASS |
| 268 | negative | turn on the sun | FAIL | PASS |
| 271 | multi_and | turn on living room light and come here | PASS | FAIL |
| 289 | negative | uh order pizza online okay | FAIL | PASS |
| 294 | single_clean | unlock ahmed door | PASS | FAIL |
| 298 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 305 | negative | please turn on the sun now | FAIL | PASS |
| 306 | named_slot | unlock tom door | PASS | FAIL |
| 310 | fuzzy_per | okay um set thermostat to seventy now | PASS | FAIL |
| 314 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 317 | single_clean | set thermostat to seventy | PASS | FAIL |
| 319 | cross_domain | set timer for five minutes then turn off room fan | PASS | FAIL |
| 322 | single_clean | set timer for five minutes | PASS | FAIL |
| 328 | named_slot | play john playlist | PASS | FAIL |
| 332 | fuzzy_per | record room camera okay | PASS | FAIL |
| 338 | negative | um order pizza online okay okay | FAIL | PASS |
| 343 | stt_noise | 09309582903 er uhh $$$ hey like stop | PASS | FAIL |
| 345 | negative | turn on the sun | FAIL | PASS |
| 346 | fuzzy_per | patol kitchen | PASS | FAIL |
| 348 | negative | go to mars | FAIL | PASS |
| 351 | glued_dgs | setThermostatToSeventy | PASS | FAIL |
| 358 | named_slot | play anna playlist | PASS | FAIL |
| 362 | stt_noise | i want to switch on bedroom a lamp okay hmm | PASS | FAIL |
| 364 | negative | can you order pizza online please | FAIL | PASS |
| 368 | glued_dgs | setTimerForFiveMinutes | PASS | FAIL |
| 370 | negative | turn on the sun | FAIL | PASS |
| 385 | negative | what is the time | FAIL | PASS |
| 392 | negative | go to mars | FAIL | PASS |
| 395 | negative | what is the time | FAIL | PASS |
| 396 | negative | what is the time | FAIL | PASS |
| 403 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 407 | glued_dgs | setTimerForFiveMinutes | PASS | FAIL |
| 412 | negative | hey turn on the sun right now | FAIL | PASS |
| 414 | fuzzy_per | turn no front light | PASS | FAIL |
| 428 | stt_noise | OKAY UM ER DISARM ALARM RIGHT NOW OKAY THIS 120 | PASS | FAIL |
| 433 | negative | um order pizza online okay thanks | FAIL | PASS |
| 439 | stt_noise | ER ER COULD THIS YOU PLAY MUSIC | PASS | FAIL |
| 450 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 455 | stt_noise | xxx previous song okay | PASS | FAIL |
| 456 | stt_noise | well can you cancel a timer 120 please | PASS | FAIL |
| 458 | temporal_then | set timer for five minutes then turn off room fan | PASS | FAIL |
| 464 | multi_and | turn on living room light and come here | PASS | FAIL |
| 475 | stt_noise | please switch on living hmm room lamp xxx okay tha… | PASS | FAIL |
| 476 | single_clean | record front camera | PASS | FAIL |
| 485 | named_slot | play john playlist | PASS | FAIL |
| 494 | stt_noise | i want to turn on uhh kate living light right now … | PASS | FAIL |
| 497 | named_slot | unlock mary hallway door | PASS | FAIL |
| 500 | glued_dgs | lockLivingRoomDoor | PASS | FAIL |

## Notes for MSc report

- **Latency** on host PC is for relative comparison across categories; ESP32 absolute values will differ (use `bench` on Serial CLI for on-device numbers).
- **Categories** map to TinyCFG features: DGS (glued_dgs), PER/fuzzy (fuzzy_per), composition operators (multi_and / multi_or / multi_juxta).
- **Negative cases** validate that the parser rejects out-of-grammar input.

---

## LaTeX tables (copy into thesis)

```latex
\begin{table}[htbp]
\centering
\caption{TinyCFG parsing accuracy by category}
\label{tab:tcfg-acc}
\begin{tabular}{lrrr}
\toprule
Category & Cases & Accuracy & Mismatches \\
\midrule
single clean & 55 & 85.5% & 8 \\
multi and & 45 & 93.3% & 3 \\
multi or & 25 & 100.0% & 0 \\
multi juxta & 35 & 100.0% & 0 \\
named slot & 55 & 83.6% & 9 \\
stt noise & 80 & 73.8% & 21 \\
glued dgs & 45 & 88.9% & 5 \\
fuzzy per & 45 & 77.8% & 10 \\
temporal then & 35 & 82.9% & 6 \\
cross domain & 35 & 91.4% & 3 \\
negative & 45 & 51.1% & 22 \\
**Overall** & 500 & **82.6%** &  \\
\bottomrule
\end{tabular}
\end{table}

\begin{table}[htbp]
\centering
\caption{TinyCFG parse latency by category ($\mu$s)}
\label{tab:tcfg-lat}
\begin{tabular}{lrrrr}
\toprule
Category & Mean µs & Min µs & Max µs & P95 µs \\
\midrule
single clean & 22.7 & 0.0 & 1558.0 & 0.0 \\
multi and & 50.4 & 0.0 & 1582.0 & 0.0 \\
multi or & 42.8 & 0.0 & 1550.0 & 0.0 \\
multi juxta & 39.5 & 0.0 & 1598.0 & 0.0 \\
named slot & 32.8 & 0.0 & 1559.0 & 0.0 \\
stt noise & 63.4 & 0.0 & 1581.0 & 245.0 \\
glued dgs & 26.9 & 0.0 & 1549.0 & 0.0 \\
fuzzy per & 33.9 & 0.0 & 1554.0 & 33.5 \\
temporal then & 54.3 & 0.0 & 1549.0 & 86.2 \\
cross domain & 57.0 & 0.0 & 1560.0 & 43.1 \\
negative & 37.8 & 0.0 & 1573.0 & 33.5 \\
**Overall** & **42.3** & 0.0 & 1598.0 & **54.3** \\
\bottomrule
\end{tabular}
\end{table}
```
