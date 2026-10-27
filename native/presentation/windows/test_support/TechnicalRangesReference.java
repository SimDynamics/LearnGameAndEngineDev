import com.learngraphicsprogramming.app.narration.NarrationLanguage;
import com.learngraphicsprogramming.app.narration.ProgrammingTechnicalRanges;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Collectors;

/** Test oracle only: executes the canonical Kotlin analyzer, without Android UI. */
public class TechnicalRangesReference {
    static String decode(String value) {
        StringBuilder result = new StringBuilder();
        for (int at = 0; at < value.length(); at++) {
            char next = value.charAt(at);
            if (next == '\\') {
                next = value.charAt(++at);
                next = switch (next) { case 'n' -> '\n'; case 'r' -> '\r'; case 't' -> '\t'; case '\\' -> '\\'; default -> throw new IllegalArgumentException("Invalid fixture escape"); };
            }
            result.append(next);
        }
        return result.toString();
    }
    static String ranges(List<ProgrammingTechnicalRanges.Range> values) {
        if (values.isEmpty()) return "-";
        return values.stream().map(range -> range.getStart() + ":" + range.getEndExclusive()).collect(Collectors.joining(","));
    }
    public static void main(String[] arguments) throws Exception {
        StringBuilder output = new StringBuilder();
        for (String line : Files.readAllLines(Path.of(arguments[0]), StandardCharsets.UTF_8)) {
            if (line.startsWith("#")) { output.append(line).append('\n'); continue; }
            String[] cells = line.split("\t", -1);
            NarrationLanguage language = switch (cells[0]) { case "es" -> NarrationLanguage.SPANISH; case "fr" -> NarrationLanguage.FRENCH; case "en-GB" -> NarrationLanguage.BRITISH_ENGLISH; default -> NarrationLanguage.AMERICAN_ENGLISH; };
            var result = ProgrammingTechnicalRanges.INSTANCE.analyze(decode(cells[1]), language);
            output.append(line).append('\t').append(ranges(result.getFragments())).append('\t').append(ranges(result.getWrappers())).append('\n');
        }
        Files.writeString(Path.of(arguments[1]), output, StandardCharsets.UTF_8);
    }
}
