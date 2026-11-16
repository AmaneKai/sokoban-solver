// Command line front end: a map on stdin, the search time in ms and then the moves on stdout.
mod search;

use std::io::Read;
use std::time::Instant;

fn main() {
    let mut text = String::new();
    std::io::stdin().read_to_string(&mut text).expect("stdin is not text");

    let mut rows: Vec<&str> = text.lines().collect();
    while rows.last().is_some_and(|row| row.trim().is_empty()) {
        rows.pop();
    }
    let width = rows.iter().map(|row| row.chars().count()).max().unwrap_or(0);
    if width == 0 {
        eprintln!("rust: empty map");
        std::process::exit(2);
    }

    // The ground and the items are separate grids. Short rows are padded with walls.
    let (mut ground, mut items) = (Vec::new(), Vec::new());
    for row in &rows {
        let mut cells: Vec<char> = row.chars().collect();
        cells.resize(width, '#');
        ground.push(
            cells
                .iter()
                .map(|&c| match c {
                    '#' => '#',
                    '.' | '*' | '+' => '.',
                    _ => ' ',
                })
                .collect::<String>(),
        );
        items.push(
            cells
                .iter()
                .map(|&c| match c {
                    '$' | '*' => '$',
                    '@' | '+' => '@',
                    _ => ' ',
                })
                .collect::<String>(),
        );
    }

    let start = Instant::now();
    let moves = search::solve(width, rows.len(), &ground, &items);
    println!("{:.3}\n{}", start.elapsed().as_secs_f64() * 1000.0, moves);
}
