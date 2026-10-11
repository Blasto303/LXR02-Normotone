/*
 * MenuText.h — LXR-02 menu label strings.
 * Ported from original LXR AVR MenuText.h by Julian Schmidt.
 * PROGMEM removed — plain const arrays, Cortex-M7 accesses flash directly.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef MENUTEXT_H_
#define MENUTEXT_H_

/* DTYPE_MENU stores its menu table id in the high nibble of one byte. Id 0 is
** valid and is reserved here for track scale so the new 21-entry scale menu can
** use the normal menu dtype path without widening the packed dtype table. */
#define MENU_TRACK_SCALE    0
#define MENU_FILTER         1
#define MENU_WAVEFORM       2
#define MENU_AUDIO_OUT      3
#define MENU_ROLL_RATES     4
#define MENU_SYNC_RATES     5
#define MENU_LFO_WAVES      6
#define MENU_RETRIGGER      7
#define MENU_NEXT_PATTERN   8
#define MENU_SEQ_QUANT      9
#define MENU_TRANS          10
#define MENU_MIDI           11
#define MENU_MIDI_ROUTING   12
#define MENU_MIDI_FILTERING 13
#define MENU_PPQ            14
#define MENU_EXT_SYNC       15
static const char menuText_ok[]    = "ok ";
static const char menuText_off[]   = "off";
static const char menuText_on[]    = "on ";
static const char menuText_mix[]   = "mix";
static const char menuText_fm[]    = "fm ";
static const char menuText_dash[]  = "---";
static const char menuText_blank[] = "   ";
static const char menuText_any[]   = "any";

/* Each table: [0]=count, [1..n]=entries, 3+1 chars each */
static const char ppqNames[][4] = {
    {5}, {"1"}, {"4"}, {"8"}, {"16"}, {"32"},
};
static const char midiModes[][4] = {
    {2}, {"trg"}, {"nte"},
};
static const char quantisationNames[][4] = {
    {5}, {"off"}, {"8"}, {"16"}, {"32"}, {"64"},
};
static const char transientNames[][4] = {
    {14},
    {"Snp"},{"Ofs"},{"Clk"},{"Ck2"},{"Tik"},{"Kik"},{"Rim"},
    {"Drp"},{"Hat"},{"Clp"},{"Kk2"},{"Snr"},{"Tom"},{"Sp2"},
};
static const char nextPatternNames[][4] = {
    {23},
    /*
     * Retired Pattern-next labels.
     *
     * The Pattern Settings page no longer exposes next/repeat controls, and the
     * sequencer ignores Pattern-only switches. Keep the table only so any stale
     * DTYPE_MENU reference has bounded display strings until Phase 4 rebuilds
     * Pattern storage/menu ownership.
     */
    {"p1"},{"p2"},{"p3"},{"p4"},{"p5"},{"p6"},{"p7"},{"p8"},
    {"p9"},{"p10"},{"p11"},{"p12"},{"p13"},{"p14"},{"p15"},{"p16"},
    {"r2"},{"r3"},{"r4"},{"r5"},{"r6"},{"r7"},{"r8"},
};
/*
 * LFO retrigger selector names (S076 P2).
 *
 * The leading byte is the selectable-entry count. Index 7 is the new "scn"
 * option (LFO_RETRIGGER_SCENE): a Scene change resets this LFO's phase to its
 * scaled offset, while every voice value keeps the existing voice retrigger
 * behaviour. Values 0..6 are unchanged.
 */
static const char retriggerNames[][4] = {
    {8}, {"off"},{"v1"},{"v2"},{"v3"},{"v4"},{"v5"},{"v6"},{"scn"},
};
static const char lfoWaveNames[][4] = {
    {8}, {"sin"},{"tri"},{"sup"},{"sdn"},{"sqr"},{"rnd"},{"xup"},{"xdn"},
};
/* Three-character display names for the shared LFO polarity selector.
** Clients: descriptor ROW("lfo_polarity", ..., DTYPE_LFO_POLARITY), compact
** value formatting, and single-parameter edit formatting. Outputs: "neg",
** "pos", and "bi " while the stored numeric value remains aligned with
** mod_node_polarity_t: 0 negative, 1 positive, 2 bipolar. */
static const char lfoPolarityNames[][4] = {
    {3}, {"neg"},{"pos"},{"bi"},
};
static const char rollRateNames[][4] = {
    {14},
    {"One"},{"1/1"},{"1/2"},{"1/3"},{"1/4"},{"1/6"},{"1/8"},
    {"12"},{"16"},{"24"},{"32"},{"48"},{"64"},{"128"},
};
static const char syncRateNames[][4] = {
    {12},
    {"off"},{"4/1"},{"2/1"},{"1/1"},{"1/2"},{"1/3"},
    {"1/4"},{"1/6"},{"1/8"},{"12"},{"16"},{"32"},
};
static const char extSyncNames[][4] = {
    {5}, {"off"}, {"usb"}, {"din"}, {"pls"}, {"aut"},
};
/*
 * Per-track play-mode names (S078 §4.2).
 *
 * The leading byte is the selectable-entry count; entries follow in the stored
 * play-mode order fwd/rev/pip/rnd/onc/1fr. Clients: the STEP track-settings
 * `run` cell and the menu.c run-mode formatter (menu_getPlayModeName).
 */
static const char trackPlayModeNames[][4] = {
    {6}, {"fwd"},{"rev"},{"pip"},{"rnd"},{"onc"},{"1fr"},
};
static const char waveformNames[][4] = {
    {6}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},{"Cym"},
};
static const char outputNames[][7] = {
    {6}, {"St1"},{"St2"},{"L1"},{"R1"},{"L2"},{"R2"},
};
static const char filterTypes[][8] = {
    {8}, {"LP"},{"HP"},{"BP"},{"UBP"},{"Nch"},{"Pek"},{"LP2"},{"off"},
};
static const char midiRoutingNames[][6] = {
    {6}, {"off"},{"U2M"},{"M2M"},{"A2M"},{"M2U"},{"M2A"},
};
static const char midiFilterNames[][16] = {
    {16},
    {"off"},{"N"},{"R"},{"RN"},{"C"},{"CN"},{"CR"},{"CRN"},
    {"P"},{"PN"},{"PR"},{"PRN"},{"PC"},{"PCN"},{"PCR"},{"all"},
};
static const char shortNames[][4] = {
    {""}, {"coa"},{"fin"},{"atk"},{"dec"},{"eg2"},{"mod"},{"amt"},
    {"frq"},{"drv"},{"vol"},{"pan"},{"noi"},{"rpt"},{"mix"},
    {"res"},{"typ"},{"f1"},{"f2"},{"g1"},{"g2"},{"wav"},{"dst"},
    {"snc"},{"rtg"},{"ofs"},{"voi"},{"slp"},{"d1"},{"d2"},
    {"eqg"},{"eqf"},
    {"rol"},{"mrp"},{"nte"},{"prb"},{"stp"},{"len"},{"rot"},
    {"bpm"},{"ch"},{"out"},{"fxm"},{"nxt"},{"mod"},{"vel"},
    {"fch"},{"flw"},{"qnt"},{"trk"},{"val"},{"shu"},{"ssv"},
    {"x"},{"y"},{"flx"},{"mid"},{"mrt"},{"txf"},{"rxf"},
    {"cki"},{"co1"},{"co2"},{"pcr"},{"cpu"},{"oit"},{"sca"},
    /* PERF per-voice Morph compact labels: voice number + "vm". */
    {"1vm"},{"2vm"},{"3vm"},{"4vm"},{"5vm"},{"6vm"},
    /* Read-only Pattern pool-use widget label. */
    {"pts"},
    /* Requested short label for the persistent Global AutoSave switch. */
    {"ats"},
    /* S074 bus compressor compact labels. */
    {"cmp"},{"cam"},{"ctm"},{"csc"},
    /*
     * S078 per-track run mode compact label (S078 P4: renamed from `mod`).
     * `run` matches the FX sequencer's run-mode cell (menuEffects.c), so the
     * same concept reads the same on the Effect page and the STEP track
     * settings. Accessor: valueNames[TEXT_TRACK_PLAY_MODE] (menu.c).
     */
    {"run"},
    /* S079 step-special compact labels: cdn/tim/rol/rtm/rfl (fix 2, P2). */
    {"cdn"},{"tim"},{"rol"},{"rtm"},{"rfl"},
    /* S080 E4/G10: BAR overlay, scene globals, and chain labels. */
    {"pb<"},{"pn<"},{"pb>"},{"pn>"},
    {"scd"},{"qcd"},{"qac"},{"pcr"},
    {"cn1"},{"nx1"},{"cn2"},{"nx2"},
};

static const char catNames[][16] = {
    {""}, {"Oscilltr"},{"Veloc EG"},{"Mod EG"},{"PitchMod"},
    {"FM"},{"Voice"},{"Noise"},{"Nois/Osc"},{"Filter"},
    {"Mod Osc"},{"LFO"},{"Transnt"},{"EQ"},
    {"Pattern"},{"Sound"},{"Step"},{"Euklid"},
    {"Global"},{"Velocity"},{"Parametr"},{"Sequencr"},
    {"Generatr"},{"MIDI"},{"Trigger"},
    /* S074: CAT_SCENE, the full-view category of Scene-owned cells. */
    {"Scene"},
    /*
     * S078 P4: CAT_TRACK, the full-view category of per-track Pattern
     * settings. The clicked-in view of the STEP run-mode cell reads
     * "Track   RunMode", in parallel with the FX sequencer's
     * "FX Seq  RunMode". Accessor: valueNames[TEXT_TRACK_PLAY_MODE].
     */
    {"Track"},
    /* S080 E4: BAR overlay settings category. */
    {"Prfrmnc"},
    /* S080 G10: scene chain settings category. */
    {"Chain"},
};

static const char longNames[][16] = {
    {""}, {"Coarse"},{"Fine"},{"Attack"},{"Decay"},{"Amount"},
    {"Frequncy"},{"Overdriv"},{"Volume"},{"Panning"},{"Mix"},
    {"RepeatCt"},{"Resnance"},{"Type"},{"Gain"},{"Waveform"},
    {"DstParam"},{"ClockSnc"},{"Retriggr"},{"Offset"},
    {"DstVoice"},{"Slope"},{"Dcy Clsd"},{"Dcy Open"},
    {"RollRate"},{"Morph"},{"Note"},{"Prbablty"},{"Number"},
    {"Length"},{"Steps"},{"Rotation"},{"Tempo"},{"SyncInpt"},
    {"AudioOut"},{"Channel"},{"FX Morph"},{"NextPatt"},{"Phase"},{"Mode"},
    {"Vol mod"},{"Fetch"},{"Follow"},{"Quantize"},{"AutTrack"},
    {"Aut Dest"},{"AutValue"},{"Shuffle"},{"Screensv"},
    {"X Positn"},{"Y Positn"},{"Flux"},{"Velocity"},
    {"Freqcy 1"},{"Freqcy 2"},{"Gain 1"},{"Gain 2"},
    {"Routing"},{"TxFilter"},{"RxFilter"},
    {"In PPQ"},{"Out1 PPQ"},{"Out2 PPQ"},{"Gate Mode"},{"PCReset"},
    {"CPU use time"},{"OscIntrp"},{"Scale"},
    /* Single-parameter long names for PERF per-voice Morph controls. */
    {"1 Morph"},{"2 Morph"},{"3 Morph"},{"4 Morph"},{"5 Morph"},{"6 Morph"},
    /* Read-only Pattern pool-use widget title; fits the 15-visible-char field. */
    {"PtrnStoreUse"},
    /* Requested long label; valueNames assigns it to category Global. */
    {"AutoSave"},
    /* S074 bus compressor long names. */
    {"BusComp"},{"CompAmt"},{"CompTime"},{"CompSC"},
    /* S078 per-track run mode long name (S078 P4: renamed from PlayMode to
     * match the FX sequencer's RunMode). */
    {"RunMode"},
    /* S079 step-special long names (fix 2: Conditin, % Delay; P2: Rhythm). */
    {"Conditin"},{"% Delay"},{"RollRate"},{"Rhythm"},{"RollFill"},
    /* S080 E4/F4/G10: overlay, scene-global, and chain long names. */
    {"BtnStat<"},{"BtnNote<"},{"BtnStat>"},{"BtnNote>"},
    {"ChngDiv"},{"QueDiv"},{"QueActn"},{"PCReset"},
    {"Conditn1"},{"NxtScn1"},{"Conditn2"},{"NxtScn2"},
};

/*
 * Trigger condition display names (S079 §3; fix 3 extends to track 7;
 * S080 B1 adds the loop-active pair).
 *
 * S080 B1: insert `lop` (loop active) and `!lp` (loop inactive) after `!fl`;
 * the count grows 39 -> 41 and every later condition shifts up by two.
 * The table is indexed directly by the stored condition value
 * (menu_getConditionName() reads conditionNames[value + 1]), so it must stay
 * in numeric order. The mute group is therefore emitted in the dispatch's
 * order - 1mu..7mu (indices 27..33) then !1m..!7m (34..40) - matching
 * seq_evaluateStepCondition()'s (cond - 27) % 7 / boundary cond <= 33 rule.
 * (S079 fix 3 had appended 7mu/!7m at the end of the old 6-track grouping,
 * leaving the displayed labels one group out of step; this corrects it.)
 *
 * [0]=count; [1..41] = condition indices 0..40.
 */
static const char conditionNames[][4] = {
    {41},
    {"  -"},{"r10"},{"r20"},{"r30"},{"r40"},{"r50"},{"r60"},{"r70"},
    {"r80"},{"r90"},{"r95"},
    {"1:2"},{"2:2"},{"1:3"},{"2:3"},{"3:3"},{"1:4"},{"2:4"},{"3:4"},
    {"4:4"},{"1:-"},
    {"fil"},{"!fl"},{"lop"},{"!lp"},{"lst"},{"!ls"},
    {"1mu"},{"2mu"},{"3mu"},{"4mu"},{"5mu"},{"6mu"},{"7mu"},
    {"!1m"},{"!2m"},{"!3m"},{"!4m"},{"!5m"},{"!6m"},{"!7m"},
};

/* Per-step roll rate display names (S079 §4.1). [0]=count; [1..9] = 0..8. */
static const char stepRollRateNames[][4] = {
    {9},
    {"off"},{"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 48"},{" 64"},
};

/*
 * S080 E4: PERF BAR overlay trigger-type names.
 *
 * [0]=count; [1..12] = stored value 0..11. Rendered by the static-param
 * special case in menu.c (S080 E6) because all 16 DTYPE_MENU ids are taken.
 * Order: vmx, v10, v64, v32, flm, fls, then the held roll rates r/8..128.
 * Affiliates: PAR_PERF_BTN_LT/RT, the overlay trigger dispatch (E12).
 */
static const char perfBtnTypeNames[][4] = {
    {12},
    {"vmx"},{"v10"},{"v64"},{"v32"},{"flm"},{"fls"},
    {"r/8"},{"r16"},{"r32"},{"r64"},{"r6t"},{"128"},
};

/*
 * S080 F4: scene change/queue division names (shared by scd and qcd).
 * [0]=count; [1..10] = stored value 0..9: instant, 1/8, 1/4, 1/2, 1-bar,
 * 2/3/4-bar, end-of-shortest-track, end-of-longest-track.
 */
static const char sceneChangeDivNames[][4] = {
    {10},
    {"ins"},{"1/8"},{"1/4"},{"1/2"},{"1b"},
    {"2b"},{"3b"},{"4b"},{"ens"},{"enl"},
};

/*
 * S080 F4: queued scene-change action names.
 * [0]=count; [1..5] = 0..4: normal, reset mutes, reset morphs, both, reload.
 */
static const char sceneQueueActionNames[][4] = {
    {5},
    {"nrm"},{"mut"},{"mor"},{"m+m"},{"rld"},
};

/*
 * S080 G9: scene chain condition names.
 * [0]=count; [1..79] = condition index 0..78 (nul, always-rep, always-bar,
 * repeat counts, bar counts, probabilities, bar positions, from-scene, fill
 * and loop conditions). Rendered by the chain menu static-param path (G8/G10).
 */
static const char chainConditionNames[][4] = {
    {79},
    {"nul"},{"xal"},{"bal"},
    {"x01"},{"x02"},{"x03"},{"x04"},{"x05"},{"x06"},{"x07"},{"x08"},
    {"x16"},{"x32"},{"x64"},
    {"b01"},{"b02"},{"b03"},{"b04"},{"b05"},{"b06"},{"b07"},{"b08"},
    {"b16"},{"b32"},{"b64"},
    {"r1x"},{"r2x"},{"r3x"},{"r4x"},{"r5x"},{"r6x"},{"r7x"},{"r8x"},{"r9x"},
    {"r1b"},{"r2b"},{"r3b"},{"r4b"},{"r5b"},{"r6b"},{"r7b"},{"r8b"},{"r9b"},
    {"1:4"},{"2:4"},{"3:4"},{"4:4"},
    {"1:8"},{"2:8"},{"3:8"},{"4:8"},{"5:8"},{"6:8"},{"7:8"},{"8:8"},
    {">01"},{">02"},{">03"},{">04"},{">05"},{">06"},{">07"},{">08"},
    {">09"},{">10"},{">11"},{">12"},{">13"},{">14"},{">15"},{">16"},
    {"fix"},{"fib"},{"!fx"},{"!fb"},
    {"lox"},{"lob"},{"!lx"},{"!lb"},
};

/*
 * S080 G9: scene chain next-scene names.
 * [0]=count; [1..44] = value 0..43 (self, next, previous, absolute s01..s16,
 * random up to r01..r16, relative random r+1..r+4 / r-1..r-4, stop playback).
 */
static const char chainNextNames[][4] = {
    {44},
    {"slf"},{"nxt"},{"prv"},
    {"s01"},{"s02"},{"s03"},{"s04"},{"s05"},{"s06"},{"s07"},{"s08"},
    {"s09"},{"s10"},{"s11"},{"s12"},{"s13"},{"s14"},{"s15"},{"s16"},
    {"r01"},{"r02"},{"r03"},{"r04"},{"r05"},{"r06"},{"r07"},{"r08"},
    {"r09"},{"r10"},{"r11"},{"r12"},{"r13"},{"r14"},{"r15"},{"r16"},
    {"r+1"},{"r+2"},{"r+3"},{"r+4"},
    {"r-1"},{"r-2"},{"r-3"},{"r-4"},
    {"stp"},
};

#endif /* MENUTEXT_H_ */
