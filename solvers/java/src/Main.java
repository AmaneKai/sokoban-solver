import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import solver.SokoBot;

/** Command line front end: a map on stdin, the search time in ms and then the moves on stdout. */
public class Main {
    public static void main(String[] args) throws IOException {
        String text = new String(System.in.readAllBytes(), StandardCharsets.ISO_8859_1);
        List<String> rows = new ArrayList<>(List.of(text.split("\r?\n", -1)));
        while (!rows.isEmpty() && rows.get(rows.size() - 1).isBlank())
            rows.remove(rows.size() - 1);

        int height = rows.size();
        int width = rows.stream().mapToInt(String::length).max().orElse(0);
        if (width == 0) {
            System.err.println("java: empty map");
            System.exit(2);
        }

        // The ground and the items are separate grids. Short rows are padded with walls.
        char[][] ground = new char[height][width];
        char[][] items = new char[height][width];
        for (int r = 0; r < height; r++) {
            String row = rows.get(r);
            for (int c = 0; c < width; c++) {
                char ch = c < row.length() ? row.charAt(c) : '#';
                ground[r][c] = ch == '#' ? '#' : (ch == '.' || ch == '*' || ch == '+') ? '.' : ' ';
                items[r][c] = (ch == '$' || ch == '*') ? '$' : (ch == '@' || ch == '+') ? '@' : ' ';
            }
        }

        long start = System.nanoTime();
        String moves = new SokoBot().solveSokobanPuzzle(width, height, ground, items);
        System.out.printf(java.util.Locale.ROOT, "%.3f%n%s%n", (System.nanoTime() - start) / 1e6, moves);
    }
}
