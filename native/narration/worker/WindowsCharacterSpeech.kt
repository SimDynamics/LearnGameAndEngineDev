package lgaed.narration.windows

import com.learngraphicsprogramming.app.narration.NarrationLanguage
import com.learngraphicsprogramming.app.narration.NarrationSourceProjection
import com.learngraphicsprogramming.app.narration.ReadingTextRange
import java.util.Locale

/** Literal character names, independent of C++, arithmetic and the ink role.
 * Unknown scalars retain their exact identity through a spoken Unicode U+ code. */
internal object WindowsCharacterSpeech {
    private const val MAX_PREPARED_BYTES = 8 * 1024 * 1024
    private data class Name(val es: String, val fr: String, val en: String) {
        fun inLanguage(language: NarrationLanguage) = when (language) {
            NarrationLanguage.SPANISH -> es
            NarrationLanguage.FRENCH -> fr
            else -> en
        }
    }
    private val symbols = mapOf(
        8 to Name("retroceso","retour arrière","backspace"),
        9 to Name("tabulación","tabulation","tab"),
        10 to Name("salto de línea","saut de ligne","line feed"),
        11 to Name("tabulación vertical","tabulation verticale","vertical tab"),
        12 to Name("salto de página","saut de page","form feed"),
        13 to Name("retorno de carro","retour chariot","carriage return"),
        27 to Name("escape","échappement","escape"),
        32 to Name("espacio","espace","space"),
        33 to Name("signo de exclamación","point d’exclamation","exclamation mark"),
        34 to Name("comilla doble","guillemet double","double quotation mark"),
        35 to Name("almohadilla","croisillon","number sign"),
        36 to Name("signo de dólar","signe dollar","dollar sign"),
        37 to Name("signo de porcentaje","signe pour cent","percent sign"),
        38 to Name("ampersand","esperluette","ampersand"),
        39 to Name("comilla simple","guillemet simple","single quotation mark"),
        40 to Name("paréntesis izquierdo","parenthèse gauche","left parenthesis"),
        41 to Name("paréntesis derecho","parenthèse droite","right parenthesis"),
        42 to Name("asterisco","astérisque","asterisk"),
        43 to Name("signo más","signe plus","plus sign"),
        44 to Name("coma","virgule","comma"),
        45 to Name("guion","trait d’union","hyphen"),
        46 to Name("punto","point","full stop"),
        47 to Name("barra inclinada","barre oblique","slash"),
        58 to Name("dos puntos","deux points","colon"),
        59 to Name("punto y coma","point virgule","semicolon"),
        60 to Name("signo menor que","signe inférieur à","less than sign"),
        61 to Name("signo igual","signe égal","equals sign"),
        62 to Name("signo mayor que","signe supérieur à","greater than sign"),
        63 to Name("signo de interrogación","point d’interrogation","question mark"),
        64 to Name("arroba","arobase","at sign"),
        91 to Name("corchete izquierdo","crochet gauche","left square bracket"),
        92 to Name("barra inversa","barre oblique inverse","backslash"),
        93 to Name("corchete derecho","crochet droit","right square bracket"),
        94 to Name("acento circunflejo","accent circonflexe","circumflex accent"),
        95 to Name("guion bajo","trait de soulignement","underscore"),
        96 to Name("acento grave","accent grave","grave accent"),
        123 to Name("llave izquierda","accolade gauche","left curly bracket"),
        124 to Name("barra vertical","barre verticale","vertical bar"),
        125 to Name("llave derecha","accolade droite","right curly bracket"),
        126 to Name("virgulilla","tilde","tilde"),
        160 to Name("espacio de no separación","espace insécable","no break space"),
        161 to Name("signo de apertura de exclamación","point d’exclamation renversé","inverted exclamation mark"),
        191 to Name("signo de apertura de interrogación","point d’interrogation renversé","inverted question mark")
    )
    private val spanishLetters = "a|be|ce|de|e|efe|ge|hache|i|jota|ka|ele|eme|ene|o|pe|cu|erre|ese|te|u|uve|doble uve|equis|i griega|zeta".split('|')
    private val frenchLetters = "a|bé|cé|dé|e|effe|gé|ache|i|ji|ka|elle|emme|enne|o|pé|ku|erre|esse|té|u|vé|double vé|ix|i grec|zède".split('|')
    private val englishLetters = "ay|bee|cee|dee|ee|ef|gee|aitch|eye|jay|kay|el|em|en|oh|pee|cue|ar|ess|tee|you|vee|double you|ex|why|zee".split('|')
    private val spanishDigits = "cero|uno|dos|tres|cuatro|cinco|seis|siete|ocho|nueve".split('|')
    private val frenchDigits = "zéro|un|deux|trois|quatre|cinq|six|sept|huit|neuf".split('|')
    private val englishDigits = "zero|one|two|three|four|five|six|seven|eight|nine".split('|')
    private fun digit(value: Int, language: NarrationLanguage) = when (language) {
        NarrationLanguage.SPANISH -> spanishDigits[value]
        NarrationLanguage.FRENCH -> frenchDigits[value]
        else -> englishDigits[value]
    }
    private fun letter(value: Int, language: NarrationLanguage) = when (language) {
        NarrationLanguage.SPANISH -> spanishLetters[value]
        NarrationLanguage.FRENCH -> frenchLetters[value]
        NarrationLanguage.BRITISH_ENGLISH -> if (value == 25) "zed" else englishLetters[value]
        else -> englishLetters[value]
    }
    private fun withCase(name: String, upper: Boolean, language: NarrationLanguage) = when (language) {
        NarrationLanguage.SPANISH -> "$name ${if (upper) "mayúscula" else "minúscula"}"
        NarrationLanguage.FRENCH -> "$name ${if (upper) "majuscule" else "minuscule"}"
        else -> "${if (upper) "uppercase" else "lowercase"} $name"
    }
    private fun name(scalar: Int, language: NarrationLanguage): String {
        symbols[scalar]?.let { return it.inLanguage(language) }
        if (scalar in 48..57) return digit(scalar - 48,language)
        if (scalar in 65..90 || scalar in 97..122) return withCase(letter((scalar or 32) - 97,language),scalar < 97,language)
        if ((scalar == 0xf1 || scalar == 0xd1) && language == NarrationLanguage.SPANISH)
            return withCase("eñe",scalar == 0xd1,language)
        val hexadecimal = scalar.toString(16).uppercase(Locale.ROOT).padStart(4,'0')
        val prefix = if (language == NarrationLanguage.SPANISH) "Unicode U más" else "Unicode U plus"
        return prefix + " " + hexadecimal.map { if (it in '0'..'9') digit(it - '0',language) else letter(it - 'A',language) }.joinToString(" ")
    }
    fun projection(source: String, language: NarrationLanguage): NarrationSourceProjection.Projection {
        val output = StringBuilder(); val owners = mutableListOf<ReadingTextRange>()
        var cursor = 0; var bytes = 0
        while (cursor < source.length) {
            val scalar = source.codePointAt(cursor); val width = Character.charCount(scalar)
            require(scalar !in 0xd800..0xdfff) { "Character reading requires complete Unicode scalars" }
            val label = name(scalar,language)
            val extra = label.toByteArray(Charsets.UTF_8).size + if (output.isEmpty()) 0 else 1
            require(bytes <= MAX_PREPARED_BYTES - extra) { "Character reading exceeds the bounded prepared-text size" }
            bytes += extra
            if (output.isNotEmpty()) { output.append(' '); owners += ReadingTextRange(0,0) }
            output.append(label)
            repeat(label.length) { owners += ReadingTextRange(cursor,cursor + width) }
            cursor += width
        }
        return NarrationSourceProjection.Projection(output.toString(),owners)
    }
}
