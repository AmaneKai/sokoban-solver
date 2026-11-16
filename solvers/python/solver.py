from collections import defaultdict

import numpy as np
from scipy.optimize import linear_sum_assignment

INF = 1_000_000
STATE_LIMIT = 1_000_000
DIRECTIONS = (("u", -1, 0), ("d", 1, 0), ("l", 0, -1), ("r", 0, 1))


class SokoBot:
    def solveSokobanPuzzle(self, width, height, mapData, itemsData):
        if width <= 0 or height <= 0:
            return ""

        stride = width + 2
        floor, goals, boxes, players = [], [], [], []
        for row in range(height):
            for column in range(width):
                cell = (row + 1) * stride + column + 1
                if mapData[row][column] != "#":
                    floor.append(cell)
                if mapData[row][column] == ".":
                    goals.append(cell)
                if itemsData[row][column] == "$":
                    boxes.append(cell)
                if itemsData[row][column] == "@":
                    players.append(cell)

        if len(players) != 1 or len(goals) != len(boxes) or not boxes:
            return ""
        if sorted(goals) == sorted(boxes):
            return ""
        return Search(stride, (height + 2) * stride, floor, goals, boxes, players[0]).run()


class Search:
    def __init__(self, stride, size, floor, goals, boxes, player):
        self.size = size
        self.floor = set(floor)
        self.goals = tuple(sorted(goals))
        self.start_boxes = tuple(boxes)
        self.start_player = player
        self.steps = []
        for name, row, column in DIRECTIONS:
            self.steps.append((name, row * stride + column))
        self.nodes = {}
        self.seen = defaultdict(list)
        self.buckets = defaultdict(list)
        self.heuristic_cache = {}
        self.low = INF

    def run(self):
        self.build_cost_table()
        solved_h = self.heuristic(self.goals)
        if solved_h >= INF:
            return ""

        initial = self.normalise(self.start_player, frozenset(self.start_boxes))
        self.add_solved_states(solved_h)
        if initial in self.nodes:
            return ""

        while self.buckets and len(self.nodes) < STATE_LIMIT:
            if self.expand(self.pop(), initial):
                return self.to_moves(initial)
        return ""

    def flood(self, start, boxes):
        region = {start}
        queue = [start]
        for cell in queue:
            for _, step in self.steps:
                target = cell + step
                if target in self.floor and target not in boxes and target not in region:
                    region.add(target)
                    queue.append(target)
        return region

    def normalise(self, player, boxes):
        return boxes, min(self.flood(player, boxes))

    def pop(self):
        while self.low not in self.buckets:
            self.low += 1
        bucket = self.buckets[self.low]
        entry = bucket.pop()
        if not bucket:
            del self.buckets[self.low]
        return entry

    def push(self, h, entry):
        self.buckets[h].append(entry)
        self.low = min(self.low, h)

    def add_solved_states(self, solved_h):
        goal_set = frozenset(self.goals)
        unvisited = self.floor - goal_set
        while unvisited:
            region = self.flood(next(iter(unvisited)), goal_set)
            unvisited -= region
            state = (goal_set, min(region))
            self.nodes[state] = None
            self.seen[goal_set].append(region)
            self.push(solved_h, (state, region, self.goals))

    def expand(self, entry, initial):
        state, region, cells = entry
        boxes = state[0]
        for source in cells:
            for _, step in self.steps:
                dest = source + step
                back = dest + step
                if dest not in region or dest not in self.reach:
                    continue
                if back not in self.floor or back in boxes:
                    continue
                if self.add_child(state, cells, source, dest, back) == initial:
                    return True
        return False

    def add_child(self, parent, cells, source, dest, back):
        child_boxes = (parent[0] - {source}) | {dest}
        known = self.seen[child_boxes]
        for region in known:
            if back in region:
                return None

        index = cells.index(source)
        child_cells = cells[:index] + (dest,) + cells[index + 1:]
        h = self.cached_heuristic(child_boxes, child_cells)
        if h >= INF:
            return None

        region = self.flood(back, child_boxes)
        child = (child_boxes, min(region))
        known.append(region)
        self.nodes[child] = (parent, source, dest)
        self.push(h, (child, region, child_cells))
        return child

    def cached_heuristic(self, boxes, cells):
        h = self.heuristic_cache.get(boxes)
        if h is None:
            h = self.heuristic(cells)
            self.heuristic_cache[boxes] = h
        return h

    def heuristic(self, cells):
        costs = self.cost[list(cells), :]
        rows, columns = linear_sum_assignment(costs)
        return min(int(costs[rows, columns].sum()), INF)

    def build_cost_table(self):
        self.cost = np.full((self.size, len(self.start_boxes)), INF, dtype=np.int64)
        self.reach = set()
        for column, start in enumerate(self.start_boxes):
            for cell, pushes in self.push_distances(start).items():
                self.cost[cell, column] = pushes
                self.reach.add(cell)

    def push_distances(self, start):
        distances = {start: 0}
        queue = [start]
        for cell in queue:
            for _, step in self.steps:
                target = cell + step
                can_push = target in self.floor and cell - step in self.floor
                if can_push and target not in distances:
                    distances[target] = distances[cell] + 1
                    queue.append(target)
        return distances

    def to_moves(self, state):
        pulls = []
        node = self.nodes[state]
        while node is not None:
            state, source, dest = node
            pulls.append((source, dest))
            node = self.nodes[state]

        step_names = {}
        for name, step in self.steps:
            step_names[step] = name

        boxes = set(self.start_boxes)
        player = self.start_player
        moves = []
        for source, dest in pulls:
            moves.append(self.walk(player, 2 * dest - source, boxes))
            moves.append(step_names[source - dest])
            boxes.remove(dest)
            boxes.add(source)
            player = dest
        return "".join(moves)

    def walk(self, start, goal, boxes):
        came_from: dict[int, tuple[int, str] | None] = {start: None}
        queue = [start]
        for cell in queue:
            if cell == goal:
                break
            for name, step in self.steps:
                target = cell + step
                if target in self.floor and target not in boxes and target not in came_from:
                    came_from[target] = (cell, name)
                    queue.append(target)

        path = []
        previous = came_from[goal]
        while previous is not None:
            goal, name = previous
            path.append(name)
            previous = came_from[goal]
        return "".join(reversed(path))
