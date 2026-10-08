package com.learngraphicsprogramming.app.narration.phonemes

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class EnglishMisakiLexiconTest {
    private fun lexicon(british: Boolean = false) = MisakiLexicon(JSONObject("""
        {"fraction":"fɹˈækʃən","eighth":"ˈAθ","letter":"lˈɛɾəɹ",
         "the":"ði","to":"tu","a":"A","an":"æn","am":"æm",
         "walk":"wˈɔk","start":"stˈɑɹt","move":"mˈuv","study":"stˈʌdi",
         "record":{"DEFAULT":"ɹˈɛkəɹd","VERB":"ɹəkˈɔɹd"},
         "can":{"DEFAULT":"kæn","None":"kˈæn"},"A":"ˈA","B":"bˈi"}
    """), JSONObject(), british)

    @Test fun pluralAndPossessiveUseMisakiSuffixRules() {
        assertEquals("fɹˈækʃənz", lexicon().pronounce(MisakiWord("fractions"), PhoneticContext()))
        assertEquals("ˈAθs", lexicon().pronounce(MisakiWord("eighths"), PhoneticContext()))
        assertEquals("lˈɛɾəɹz", lexicon().pronounce(MisakiWord("letter's"), PhoneticContext()))
    }

    @Test fun americanAndBritishPastTenseDifferAtAlveolarStops() {
        assertEquals("stˈɑɹɾᵻd", lexicon().pronounce(MisakiWord("started"), PhoneticContext()))
        assertEquals("stˈɑɹtɪd", lexicon(true).pronounce(MisakiWord("started"), PhoneticContext()))
        assertEquals("wˈɔkt", lexicon().pronounce(MisakiWord("walked"), PhoneticContext()))
    }

    @Test fun inflectionRestoresSilentEAndYStems() {
        assertEquals("mˈuvɪŋ", lexicon().pronounce(MisakiWord("moving"), PhoneticContext()))
        assertEquals("stˈʌdiz", lexicon().pronounce(MisakiWord("studies"), PhoneticContext()))
    }

    @Test fun functionWordsFollowRightHandPhoneticContext() {
        assertEquals("ði", lexicon().pronounce(MisakiWord("the", tag = "DT"), PhoneticContext(true)))
        assertEquals("ðə", lexicon().pronounce(MisakiWord("the", tag = "DT"), PhoneticContext(false)))
        assertEquals("tʊ", lexicon().pronounce(MisakiWord("to", tag = "TO"), PhoneticContext(true)))
        assertEquals("tə", lexicon().pronounce(MisakiWord("to", tag = "TO"), PhoneticContext(false)))
        assertEquals("kˈæn", lexicon().pronounce(MisakiWord("can"), PhoneticContext()))
        assertEquals("kæn", lexicon().pronounce(MisakiWord("can"), PhoneticContext(true)))
    }

    @Test fun suppliedPennTreebankTagsResolveDictionaryHomographs() {
        assertEquals("ɹˈɛkəɹd", lexicon().pronounce(MisakiWord("record", tag = "NN"), PhoneticContext()))
        assertEquals("ɹəkˈɔɹd", lexicon().pronounce(MisakiWord("record", tag = "VB"), PhoneticContext()))
        assertThrows(EnglishMisakiException::class.java) {
            lexicon().pronounce(MisakiWord("record"), PhoneticContext())
        }
    }

    @Test fun unknownWordsNeverBecomeGrammaticalLettersOrRawText() {
        assertThrows(EnglishMisakiException::class.java) {
            lexicon().pronounce(MisakiWord("blorpleunlikelyword"), PhoneticContext())
        }
    }

    @Test fun stressAdjustmentKeepsUpstreamVowelPosition() {
        assertEquals("kˌæt", MisakiStress.apply("kæt", 0.5))
        assertEquals("kˈæt", MisakiStress.apply("kæt", 2.0))
        assertEquals("kˌæt", MisakiStress.apply("kˈæt", -0.5))
        assertEquals("kæt", MisakiStress.apply("kˈæt", -2.0))
    }
}
