package com.learngraphicsprogramming.app.narration

data class KokoroVoice(val id: String, val language: NarrationLanguage, val sha256: String) {
    val label: String get() = id.substringAfter('_').replaceFirstChar { it.uppercase() }
    val artifact: KokoroModelManifest.Artifact get() = KokoroModelManifest.Artifact(
        "$id.bin", "voices/$id.bin", 522_240L, sha256)
}

/** Official named voices from the pinned Kokoro-82M v1.0 catalogue. */
object KokoroVoiceCatalog {
    val voices = listOf(
        KokoroVoice("af_alloy", NarrationLanguage.AMERICAN_ENGLISH, "c4a6b876047fd7fb472edf4ebd63cfac7c3b958a7cae7c106e8f038ca6308c45"),
        KokoroVoice("af_aoede", NarrationLanguage.AMERICAN_ENGLISH, "4a004c33430762e2461eedb2013fad808ef4ab3121f5300f554476caf58d8361"),
        KokoroVoice("af_bella", NarrationLanguage.AMERICAN_ENGLISH, "f69d836209b78eb8c66e75e3cda491e26ea838a3674257e9d4e5703cbaf55c8b"),
        KokoroVoice("af_heart", NarrationLanguage.AMERICAN_ENGLISH, "d583ccff3cdca2f7fae535cb998ac07e9fcb90f09737b9a41fa2734ec44a8f0b"),
        KokoroVoice("af_jessica", NarrationLanguage.AMERICAN_ENGLISH, "a240a5e3c15b43563d6e923bdca8ef5613a23471d9b77653694012435df23bd8"),
        KokoroVoice("af_kore", NarrationLanguage.AMERICAN_ENGLISH, "9be5221b6a941c04b561959b8ff0b06e809444dcc4ab7e75a7b23606f691819e"),
        KokoroVoice("af_nicole", NarrationLanguage.AMERICAN_ENGLISH, "cd2191ab31b914ed7b318416b0e4440fdf392ddad9106a060819aa600a64f59a"),
        KokoroVoice("af_nova", NarrationLanguage.AMERICAN_ENGLISH, "18778272caa0d0eebaea251c35fd635f038434f9eee5e691d02a174bd328414f"),
        KokoroVoice("af_river", NarrationLanguage.AMERICAN_ENGLISH, "00a2bcf82b1d86e8f19902ede58c65ccf6c0e43b44b7d74fad54e5d8933c9c30"),
        KokoroVoice("af_sarah", NarrationLanguage.AMERICAN_ENGLISH, "4409fbc125afabacc615d94db5398d847006a737b0247d6892b7a9a0007a2f0a"),
        KokoroVoice("af_sky", NarrationLanguage.AMERICAN_ENGLISH, "4435255c9744f3f31659e0d714ab7689bf65d9e77ec1cce060f083912614f0b9"),
        KokoroVoice("am_adam", NarrationLanguage.AMERICAN_ENGLISH, "162b035ed91cfc48b6046982184c645f72edcdd1b82843347f605d7bf7b15716"),
        KokoroVoice("am_echo", NarrationLanguage.AMERICAN_ENGLISH, "3968b92c3c4cd1c4416dbded36c13eaa388a90d5788d02a13e4d781f5f8cf3c3"),
        KokoroVoice("am_eric", NarrationLanguage.AMERICAN_ENGLISH, "e8b5be17edd1e3636901ce7598baafe2dc8dd8ff707a0c23bf9e461add7e2832"),
        KokoroVoice("am_fenrir", NarrationLanguage.AMERICAN_ENGLISH, "c27989f741f7ee34d273a39d8a595cc0837d35f5ced9a29b7cc162614616df43"),
        KokoroVoice("am_liam", NarrationLanguage.AMERICAN_ENGLISH, "52403be32fd047c6a44517cb0bcd6b134f2a18baa73e70ef41651e0eab921ade"),
        KokoroVoice("am_michael", NarrationLanguage.AMERICAN_ENGLISH, "1d1f21dd8da39c30705cd4c75d039d265e9bc4a2a93ed09bc9e1b1225eb95ba1"),
        KokoroVoice("am_onyx", NarrationLanguage.AMERICAN_ENGLISH, "da5d135b424164916d75a68ffb4c2abce3d7d5ccc82dd1ee6cf447ce286145e6"),
        KokoroVoice("am_puck", NarrationLanguage.AMERICAN_ENGLISH, "fcf73c989033e9233e0b98713eca600c8c74dcc1614b37009d5450ff4a2274a0"),
        KokoroVoice("am_santa", NarrationLanguage.AMERICAN_ENGLISH, "61150cf726ab6c5ed7a99f90a304f91f5a72c00c592e89ec94e5df11c319227a"),
        KokoroVoice("bf_alice", NarrationLanguage.BRITISH_ENGLISH, "08afa6ba24da61ea5e8efa139e5aadc938d83f0a6da5a900adaf763ac1da5573"),
        KokoroVoice("bf_emma", NarrationLanguage.BRITISH_ENGLISH, "669fe0647f9dd04fcab92f1439a40eeb4c8b4ab1f82e4996fe3d918ce4a63b73"),
        KokoroVoice("bf_isabella", NarrationLanguage.BRITISH_ENGLISH, "3754352c4aaa46d17f27654ab7518d65b62ad6163a0f55a5f4330c2da2c4e94f"),
        KokoroVoice("bf_lily", NarrationLanguage.BRITISH_ENGLISH, "5e0ee32ebe64a467124976b14e69590746f1c4ce41a12b587a50c862edfea335"),
        KokoroVoice("bm_daniel", NarrationLanguage.BRITISH_ENGLISH, "6b3194bbceffb746733cbc22c8f593dd44e401a71d53895a2dca891bc595a1e8"),
        KokoroVoice("bm_fable", NarrationLanguage.BRITISH_ENGLISH, "f889083196807b4adb15e9204252165f503b8d33d3982e681c52443c49d798f1"),
        KokoroVoice("bm_george", NarrationLanguage.BRITISH_ENGLISH, "c4b235a4c1f2cd3b939fed08b899ce9385638b763f7b73a59616c4fc9bd6c9bc"),
        KokoroVoice("bm_lewis", NarrationLanguage.BRITISH_ENGLISH, "b8f671cef828c30e66fdf0b0756a76bba58f6bb3398cbbf27058642acbcedb97"),
        KokoroVoice("ef_dora", NarrationLanguage.SPANISH, "f66ec66bd295acb18372e37008533a9a3228483ccd294e7538d5d9294ac9a532"),
        KokoroVoice("em_alex", NarrationLanguage.SPANISH, "27809e9eafdcbcfff90a3016c697568676531de2a2c39cee29c96c7bd6b83e95"),
        KokoroVoice("em_santa", NarrationLanguage.SPANISH, "ad43b774e1ca24d05c6161297d8aeb770ac3d29bb95daf516727af5f7d543683"),
        KokoroVoice("ff_siwis", NarrationLanguage.FRENCH, "a35f5675ad08948e326ae75fd0ea16ba5d0042e4f76b5f3d1df77d0a48c54861"),
    )
    fun find(id: String): KokoroVoice? = voices.firstOrNull { it.id == id }
    fun forLanguage(language: NarrationLanguage) = voices.filter { it.language == language }
}
