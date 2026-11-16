use std::collections::BTreeMap;
use std::rc::Rc;

use fixedbitset::FixedBitSet;
use pathfinding::directed::bfs::{bfs, bfs_reach};
use pathfinding::directed::dijkstra::dijkstra_all;
use pathfinding::kuhn_munkres::kuhn_munkres_min;
use pathfinding::matrix::Matrix;
use rustc_hash::FxHashMap;
use smallvec::SmallVec;

const INF: i64 = 1_000_000;
const STATE_LIMIT: usize = 1_000_000;
const DIRECTIONS: [(char, isize, isize); 4] =
    [('u', -1, 0), ('d', 1, 0), ('l', 0, -1), ('r', 0, 1)];

type Cell = u16;
type Boxes = SmallVec<[Cell; 16]>;
type State = (Boxes, Cell);

struct Entry {
    id: u32,
    region: Rc<FixedBitSet>,
    cells: Boxes,
}

fn offset(cell: usize, step: isize) -> usize {
    cell.wrapping_add_signed(step)
}

pub fn solve(width: usize, height: usize, map_rows: &[String], item_rows: &[String]) -> String {
    if width == 0 || height == 0 {
        return String::new();
    }
    let stride = width + 2;
    let size = (height + 2) * stride;
    if size > Cell::MAX as usize {
        return String::new();
    }

    let map: Vec<Vec<char>> = map_rows.iter().map(|row| row.chars().collect()).collect();
    let items: Vec<Vec<char>> = item_rows.iter().map(|row| row.chars().collect()).collect();
    let at = |grid: &[Vec<char>], row: usize, column: usize| {
        grid.get(row).and_then(|r| r.get(column)).copied().unwrap_or(' ')
    };

    let (mut floor, mut goals, mut boxes, mut players) = (vec![], vec![], vec![], vec![]);
    for row in 0..height {
        for column in 0..width {
            let cell = ((row + 1) * stride + column + 1) as Cell;
            if at(&map, row, column) != '#' {
                floor.push(cell);
            }
            if at(&map, row, column) == '.' {
                goals.push(cell);
            }
            if at(&items, row, column) == '$' {
                boxes.push(cell);
            }
            if at(&items, row, column) == '@' {
                players.push(cell);
            }
        }
    }

    if players.len() != 1 || goals.len() != boxes.len() || boxes.is_empty() {
        return String::new();
    }
    let (mut sorted_goals, mut sorted_boxes) = (goals.clone(), boxes.clone());
    sorted_goals.sort_unstable();
    sorted_boxes.sort_unstable();
    if sorted_goals == sorted_boxes {
        return String::new();
    }

    Search::new(stride, size, &floor, sorted_goals, boxes, players[0]).run()
}

struct Search {
    size: usize,
    n: usize,
    floor: Vec<bool>,
    goals: Boxes,
    start_boxes: Vec<Cell>,
    start_player: Cell,
    steps: [(char, isize); 4],
    ids: FxHashMap<State, u32>,
    parents: Vec<Option<(u32, Cell, Cell)>>,
    seen: FxHashMap<Boxes, Vec<Rc<FixedBitSet>>>,
    buckets: BTreeMap<i64, Vec<Entry>>,
    heuristic_cache: FxHashMap<Boxes, i64>,
    cost: Vec<i64>,
    reach: Vec<bool>,
}

impl Search {
    fn new(
        stride: usize,
        size: usize,
        floor_cells: &[Cell],
        goals: Vec<Cell>,
        boxes: Vec<Cell>,
        player: Cell,
    ) -> Self {
        let mut floor = vec![false; size];
        for &cell in floor_cells {
            floor[cell as usize] = true;
        }
        let steps = DIRECTIONS.map(|(name, row, column)| (name, row * stride as isize + column));
        Search {
            size,
            n: boxes.len(),
            floor,
            goals: goals.into_iter().collect(),
            start_boxes: boxes,
            start_player: player,
            steps,
            ids: FxHashMap::default(),
            parents: Vec::new(),
            seen: FxHashMap::default(),
            buckets: BTreeMap::new(),
            heuristic_cache: FxHashMap::default(),
            cost: Vec::new(),
            reach: Vec::new(),
        }
    }

    fn run(&mut self) -> String {
        self.build_cost_table();
        let goals = self.goals.clone();
        let solved_h = self.heuristic(&goals);
        if solved_h >= INF {
            return String::new();
        }

        let initial = self.normalise(self.start_player, &self.start_boxes.clone());
        self.add_solved_states(solved_h);
        if self.ids.contains_key(&initial) {
            return String::new();
        }

        while !self.buckets.is_empty() && self.ids.len() < STATE_LIMIT {
            let entry = self.pop();
            if self.expand(entry, &initial) {
                let id = self.ids[&initial];
                return self.to_moves(id).unwrap_or_default();
            }
        }
        String::new()
    }

    fn open_neighbors<'a>(
        &'a self,
        cell: Cell,
        boxes: &'a [Cell],
    ) -> impl Iterator<Item = Cell> + 'a {
        self.steps
            .iter()
            .map(move |&(_, step)| offset(cell as usize, step))
            .filter(move |&target| self.floor[target] && !boxes.contains(&(target as Cell)))
            .map(|target| target as Cell)
    }

    fn flood(&self, start: Cell, boxes: &[Cell]) -> (FixedBitSet, Cell) {
        let mut region = FixedBitSet::with_capacity(self.size);
        let mut lowest = start;
        for cell in bfs_reach(start, |&cell| self.open_neighbors(cell, boxes)) {
            region.insert(cell as usize);
            lowest = lowest.min(cell);
        }
        (region, lowest)
    }

    fn normalise(&self, player: Cell, boxes: &[Cell]) -> State {
        let mut sorted: Boxes = boxes.iter().copied().collect();
        sorted.sort_unstable();
        (sorted, self.flood(player, boxes).1)
    }

    fn pop(&mut self) -> Entry {
        let mut lowest = self.buckets.first_entry().expect("queue is not empty");
        let entry = lowest.get_mut().pop().expect("buckets are never left empty");
        if lowest.get().is_empty() {
            lowest.remove();
        }
        entry
    }

    fn push(&mut self, h: i64, entry: Entry) {
        self.buckets.entry(h).or_default().push(entry);
    }

    fn add_solved_states(&mut self, solved_h: i64) {
        let goals = self.goals.clone();
        let mut visited = vec![false; self.size];
        for cell in 0..self.size {
            if !self.floor[cell] || visited[cell] || goals.contains(&(cell as Cell)) {
                continue;
            }
            let (region, lowest) = self.flood(cell as Cell, &goals);
            region.ones().for_each(|member| visited[member] = true);
            let region = Rc::new(region);

            let id = self.parents.len() as u32;
            self.parents.push(None);
            self.ids.insert((goals.clone(), lowest), id);
            self.seen.entry(goals.clone()).or_default().push(region.clone());
            self.push(solved_h, Entry { id, region, cells: goals.clone() });
        }
    }

    fn expand(&mut self, entry: Entry, initial: &State) -> bool {
        let Entry { id, region, cells } = entry;
        for index in 0..cells.len() {
            let source = cells[index] as usize;
            for direction in 0..4 {
                let step = self.steps[direction].1;
                let dest = offset(source, step);
                if !region.contains(dest) || !self.reach[dest] {
                    continue;
                }
                let back = offset(dest, step);
                if !self.floor[back] || cells.contains(&(back as Cell)) {
                    continue;
                }
                if self.add_child(id, &cells, index, dest, back, initial) {
                    return true;
                }
            }
        }
        false
    }

    fn add_child(
        &mut self,
        parent: u32,
        cells: &Boxes,
        index: usize,
        dest: usize,
        back: usize,
        initial: &State,
    ) -> bool {
        let source = cells[index];
        let mut child_cells = cells.clone();
        child_cells[index] = dest as Cell;
        let mut child_boxes = child_cells.clone();
        child_boxes.sort_unstable();

        if let Some(known) = self.seen.get(&child_boxes) {
            if known.iter().any(|region| region.contains(back)) {
                return false;
            }
        }

        let h = self.cached_heuristic(&child_boxes, &child_cells);
        if h >= INF {
            return false;
        }

        let (region, lowest) = self.flood(back as Cell, &child_cells);
        let region = Rc::new(region);
        let child: State = (child_boxes, lowest);
        let reached = child == *initial;

        self.seen.entry(child.0.clone()).or_default().push(region.clone());
        let id = self.parents.len() as u32;
        self.parents.push(Some((parent, source, dest as Cell)));
        self.ids.insert(child, id);
        self.push(h, Entry { id, region, cells: child_cells });
        reached
    }

    fn cached_heuristic(&mut self, boxes: &Boxes, cells: &[Cell]) -> i64 {
        if let Some(&h) = self.heuristic_cache.get(boxes) {
            return h;
        }
        let h = self.heuristic(cells);
        self.heuristic_cache.insert(boxes.clone(), h);
        h
    }

    fn heuristic(&self, cells: &[Cell]) -> i64 {
        let mut costs = Matrix::new(self.n, self.n, 0i64);
        for (row, &cell) in cells.iter().enumerate() {
            for column in 0..self.n {
                costs[(row, column)] = self.cost[cell as usize * self.n + column];
            }
        }
        let (total, _) = kuhn_munkres_min(&costs);
        total.min(INF)
    }

    fn build_cost_table(&mut self) {
        self.cost = vec![INF; self.size * self.n];
        self.reach = vec![false; self.size];
        for column in 0..self.n {
            for (cell, pushes) in self.push_distances(self.start_boxes[column] as usize) {
                self.cost[cell * self.n + column] = pushes;
                self.reach[cell] = true;
            }
        }
    }

    fn push_distances(&self, start: usize) -> Vec<(usize, i64)> {
        let reached = dijkstra_all(&start, |&cell| {
            self.steps
                .iter()
                .map(move |&(_, step)| (offset(cell, step), offset(cell, -step)))
                .filter(|&(target, behind)| self.floor[target] && self.floor[behind])
                .map(|(target, _)| (target, 1i64))
        });
        std::iter::once((start, 0))
            .chain(reached.into_iter().map(|(cell, (_, pushes))| (cell, pushes)))
            .collect()
    }

    fn to_moves(&self, state: u32) -> Option<String> {
        let mut pulls = Vec::new();
        let mut node = self.parents[state as usize];
        while let Some((parent, source, dest)) = node {
            pulls.push((source as usize, dest as usize));
            node = self.parents[parent as usize];
        }

        let mut boxes = self.start_boxes.clone();
        let mut player = self.start_player as usize;
        let mut moves = String::new();
        for (source, dest) in pulls {
            let behind = offset(dest, dest as isize - source as isize);
            moves.push_str(&self.walk(player, behind, &boxes)?);
            moves.push(self.step_name(source as isize - dest as isize)?);
            boxes.retain(|&cell| cell as usize != dest);
            boxes.push(source as Cell);
            player = dest;
        }
        Some(moves)
    }

    fn step_name(&self, step: isize) -> Option<char> {
        self.steps.iter().find(|&&(_, s)| s == step).map(|&(name, _)| name)
    }

    fn walk(&self, start: usize, goal: usize, boxes: &[Cell]) -> Option<String> {
        let path = bfs(
            &(start as Cell),
            |&cell| self.open_neighbors(cell, boxes),
            |&cell| cell as usize == goal,
        )?;
        path.windows(2)
            .map(|pair| self.step_name(pair[1] as isize - pair[0] as isize))
            .collect()
    }
}
