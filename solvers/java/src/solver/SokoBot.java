package solver;
import java.util.*;

public class SokoBot 
{
    /* Map Date Structures*/
    private ArrayList<int[]> goalList = new ArrayList<>();
    private char mapData[][];
    private int width, height;
    private boolean[][] goalGrid;
    private boolean[][] deadSquares;
    private int[][] roomIds;
    private List<Integer> goalCountsByRoomId = new ArrayList<>();

    /* Hashing and State Comparison */
    private long[][][] zobristTable; // [row][col][entity type]

    /* Box Tracking */
    private int[] boxPositions = new int[50]; // Flat array: [row0, col0, row1, col1, ...]
    private int boxCount = 0;

    /* State Mangement */
    private final Comparator<SokoState> stateComparator = Comparator.
    comparingInt(SokoState::getComparator);
    private final PriorityQueue<SokoState> priorityQueue = 
        new PriorityQueue<>(stateComparator);
    private final LongHashSet exploredStates = new LongHashSet(10000000);

    /* Move Constant */
    private static final int INVALID_MOVE = -1;
    private static final int NORMAL_MOVE = 1;
    private static final int PUSH_MOVE = 2;
    
    /* Direction Constants */
    private static final int[] DIR_RIGHT = {0, 1};
    private static final int[] DIR_DOWN = {1, 0};
    private static final int[] DIR_LEFT = {0, -1};
    private static final int[] DIR_UP = {-1, 0};
    private static final int[][] DIR_LOOKUP = new int[128][];
    static 
    {
        DIR_LOOKUP['r'] = DIR_RIGHT;
        DIR_LOOKUP['d'] = DIR_DOWN;
        DIR_LOOKUP['l'] = DIR_LEFT;
        DIR_LOOKUP['u'] = DIR_UP;
    }

    /* Temporary Buffers (Memory Optimization) */
    private char[][] tempItems; // reusable buffer as a copy of the state
    private int[] tempPlayer = new int[2];
    private char[] tempRotated = new char[9];
    private char[] tempNeighborhood = new char[9];
    private boolean[] tempMatchedGoals;
    private static final int[][] ROTATION_PATTERNS = 
    {
        {0,1,2,3,4,5,6,7,8}, //0 degrees
        {2,5,8,1,4,7,0,3,6}, //90 degrees
        {8,7,6,5,4,3,2,1,0}, //180 degrees
        {6,3,0,7,4,1,8,5,2}  //270 degrees
    };

    /* Initialization Methods */

    /**
     * Creates random hash values for each postiiton and entry type 
     * (player/box) using a fixed seed of 42
     * Enables O(1) state comparison instead of O(n^2) board comparison
     * by creating a unique hash signatyre for each state. The fixed seed
     * ensures deterministic behaviour across runs while maintaining decent
     * has distribution. This is important for exploration without dupicating 
     * work
     */
    private void initializeZobristTable () 
    {
        Random rand = new Random(42); // Fixed seed for deterministic behavior
        zobristTable = new long[height][width][2]; // 0 for player, 1 for box

        for (int row = 0; row < height; row++) 
        {
            for (int col = 0; col < width; col++) 
            {
                zobristTable[row][col][0] = rand.nextLong(); // Player hash
                zobristTable[row][col][1] = rand.nextLong(); // Box hash
            }
        }
    }

    /*
     * Process the map layout to identify walls, goals, and open spaces,
     * creating data structures for tracking goal positons
     * Front load analysis to avoid repetitive work during search by 
     * precommuting key information once, we reduce per-state calculation
     */
    private void initializeMap (char[][] mapData) 
    {
        goalList.clear();
        goalGrid = new boolean[height][width];

        for (int row = 0; row < height; row++) 
        {
            for (int col = 0; col < width; col++) 
            {
                char cell = mapData[row][col];
                if (cell == '.' || cell == '*') 
                {
                    goalList.add(new int[]{row, col});
                    goalGrid[row][col] = true;
                }
            }
        }

        initializeZobristTable();
        deadSquares = precomputeStaticDeadlocks();
        precomputeRooms();
    }

    /*
     * Uses a reverse search from goals to identify positions where boxes
     * can never be moved to a goal.
     * identifies unsolvable positions once at initialization it can prune
     * around 30% - 60% of the search space in more complex puzzles
     */
    private boolean[][] precomputeStaticDeadlocks () 
    {
        boolean[][] liveSquares = new boolean[height][width];
        LinkedList<int[]> queue = new LinkedList<>(goalList);

        for (int[] goal : goalList) 
            liveSquares[goal[0]][goal[1]] = true;

        while (!queue.isEmpty()) 
        {
            int[] pullTarget = queue.poll();
            int[][] dirs = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};

            for (int[] dir : dirs) 
            {
                int pullOriginRow = pullTarget[0] - dir[0];
                int pullOriginCol = pullTarget[1] - dir[1];
                int playerRow = pullOriginRow - dir[0];
                int playerCol = pullOriginCol - dir[1];

                if (pullOriginRow >= 0 && pullOriginRow < height && 
                    pullOriginCol >= 0 && pullOriginCol < width && 
                    playerRow >= 0 && playerRow < height && 
                    playerCol >= 0 && playerCol < width && 
                    mapData[pullOriginRow][pullOriginCol] != '#' &&
                    mapData[playerRow][playerCol] != '#' &&
                    !liveSquares[pullOriginRow][pullOriginCol]) 
                {
                    liveSquares[pullOriginRow][pullOriginCol] = true;
                    queue.add(new int[]{pullOriginRow, pullOriginCol});
                }
            }
        }

        boolean[][] result = new boolean[height][width];
        
        for (int r = 0; r < height; r++) 
            for (int c = 0; c < width; c++) 
                if (mapData[r][c] != '#' && !liveSquares[r][c]) 
                    result[r][c] = true;

        return result;
    }

    /*
     * Divides the puzzle into separate "rooms" and counts goals in each
     * If a room has more boxes than goals, the puzzle is unsolvable
     */
    private void precomputeRooms () 
    {
        roomIds = new int[height][width];
        goalCountsByRoomId.clear();

        for (int r = 0; r < height; r++) 
            for (int c = 0; c < width; c++) 
                roomIds[r][c] = -1;

        int currentRoomId = 0;
        for (int r = 0; r < height; r++) 
        {
            for (int c = 0; c < width; c++) 
            {
                if (mapData[r][c] != '#' && roomIds[r][c] == -1) 
                {
                    int goalCount = floodFillRoom(new int[]{r, c}, 
                        currentRoomId);
                    goalCountsByRoomId.add(goalCount);
                    currentRoomId++;
                }
            }
        }
    }

    /*
     * Performs BFS with a queue to identify and mark all cells in a room with 
     * the same ID 
     * it enables quick room-based deadlock detection without repeating
     * the flood fill during the search
     */
    private int floodFillRoom (int[] start, int roomId) 
    {
        int goalCount = 0;
        LinkedList<int[]> queue = new LinkedList<>();
        queue.add(start);
        roomIds[start[0]][start[1]] = roomId;

        while (!queue.isEmpty()) 
        {
            int[] current = queue.poll();

            if (goalGrid[current[0]][current[1]]) 
                goalCount++;

            int[][] dirs = {DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT};

            for (int[] dir : dirs) 
            {
                int newRow = current[0] + dir[0];
                int newCol = current[1] + dir[1];

                if (newRow >= 0 && newRow < height && newCol >= 0 && 
                    newCol < width && mapData[newRow][newCol] != '#' && 
                    roomIds[newRow][newCol] == -1) 
                {
                    roomIds[newRow][newCol] = roomId;
                    queue.add(new int[]{newRow, newCol});
                }
            }
        }

        return goalCount;
    }

    /* State Mangement Methods */

    /*
     * Copies a board state using System.array.copy instad of manual loop
     * copying.
     * Takes advantage of optimized native mathods that is 2-3x faster than 
     * element by element copy. Reducing overhead.
     */
    private void deepCopyItems (char[][] source, char[][] dest) 
    {
        for (int i = 0; i < height; i++) 
            System.arraycopy(source[i], 0, dest[i], 0, width);
    }

    /*
     * Computes the initial Zobrist Hash for a board state by XORing hash
     * values for each entity position.
     * Creates a reliable hash without having to track incremental changes
     */
    private long calculateInitialHash(char[][] itemsData) 
    {
        long hash = 0;

        for (int row = 0; row < height; row++) 
        {
            for (int col = 0; col < width; col++) 
            {
                char cell = itemsData[row][col];

                if (cell == '@') 
                    hash ^= zobristTable[row][col][0]; // XOR with player hash
                
                else if (cell == '$' || cell == '*') 
                    hash ^= zobristTable[row][col][1]; // XOR with box hash
                
            }
        }

        return hash;
    }

    /*
     * Locates the player position in the current board state through direct
     * linear search.
     */
    private int[] findPlayer(char[][] itemsData) 
    {
        for (int row = 0; row < height; row++) 
            for (int col = 0; col < width; col++) 
                if (itemsData[row][col] == '@')
                    return new int[]{row, col};

        return new int[]{-1, -1};
    }

    /*
     * Standardize the initial state representation by marking boxes on 
     * goals as '*' instead of '$'
     *
     * Simplifies comparison logic throughtout the solve by elimiating special
     * cases
     */
    private void fixInitialState(char[][] itemsData) 
    {
        for (int row = 0; row < height; row++) 
            for (int col = 0; col < width; col++) 
                if (itemsData[row][col] == '$' && goalGrid[row][col]) 
                    itemsData[row][col] = '*'; // Mark box on goal
    }

    /*
     * Constructs the solution path by backtracking through parent references
     * and reversing its sequence
     *
     * Uses StringBuilder with known capacity instead of string concatenation
     * avoiding O(n^2) perfomance penalty of string concatenation in loops
     */
    private String buildSolutionPath(SokoState finalState) 
    {
        StringBuilder path = new StringBuilder();
        SokoState current = finalState;

        while (current != null && current.getAction() != 's') 
        {
            path.append(current.getAction());
            current = current.getPrevState();
        }

        return path.reverse().toString();
    }


    /* Seearch Algorithm Method */
    /*
     * Main entry point that implements GBFS, initialize DS and precomp 
     * deadlocks
     * A greedy approach because we dont care about optimal solution 
     * and prioritize speed above all else
     */
    public String solveSokobanPuzzle(int width, int height, char[][] mapData,
        char[][] itemsData) 
    {
        this.width = width;
        this.height = height;
        this.mapData = mapData;

        priorityQueue.clear();
        exploredStates.clear();

        tempItems = new char[height][width];

        initializeMap(mapData);
        tempMatchedGoals = new boolean[goalList.size()];
        fixInitialState(itemsData);

        if (isDeadlocked(itemsData)) 
            return "";

        int initialHeuristic = calculateEnhancedHeuristic(itemsData);

        int[] playerPos = findPlayer(itemsData);
        long initialHash = calculateInitialHash(itemsData);

        SokoState initialState = new SokoState(itemsData, initialHeuristic,
            's', null, playerPos[0], playerPos[1], initialHash);
        priorityQueue.add(initialState);

        exploredStates.add(initialHash);

        while (!priorityQueue.isEmpty()) 
        {
            SokoState currentState = priorityQueue.poll();

            if (currentState.getHeuristic() == 0 )
                return buildSolutionPath(currentState);

            enqueuePossiblePushes(currentState);
        }

        return "";
    }

    /* 
     * Finds all possible push states from the current state by exploring in 
     * all four directions 
     *
     * Focuses only on push moves rather than all player moves dramatically
     * reducing the branching factor. This push-focused approach could explore
     * 5-10x fewer states than tracking every player position
     */
    private void enqueuePossiblePushes(SokoState currentState) 
    {
        List<SokoState> possiblePushStates = new ArrayList<>();

        explorePossiblePushStates(possiblePushStates, currentState, 'l');
        explorePossiblePushStates(possiblePushStates, currentState, 'u');
        explorePossiblePushStates(possiblePushStates, currentState, 'r');
        explorePossiblePushStates(possiblePushStates, currentState, 'd');

        possiblePushStates.forEach(priorityQueue::add);
    }

    /*
     * Revursively explores to find push moves using hash prediction for 
     * efficient prunning of explored states
     * Uses 'lurd' directin order which was found to perform faster overall
     * based on trial and error
     */
    private void explorePossiblePushStates(List<SokoState> possiblePushStates,
        SokoState currentState, char action) 
    {
        tempPlayer[0] = currentState.playerRow;
        tempPlayer[1] = currentState.playerCol;
        long hash = currentState.stateHash;
        int oldRow = currentState.playerRow;
        int oldCol = currentState.playerCol;
        int[] direction = DIR_LOOKUP[action];
        int newRow = oldRow + direction[0];
        int newCol = oldCol + direction[1];

        if (newRow < 0 || newRow >= height || newCol < 0 || 
            newCol >= width || mapData[newRow][newCol] == '#')
            return;

        char destCell = currentState.getItemsData()[newRow][newCol];

        if (destCell == ' ' || destCell == '.') 
        {
            long predictedHash = hash ^ zobristTable[oldRow][oldCol][0] ^ 
                zobristTable[newRow][newCol][0];

            if (exploredStates.contains(predictedHash))
                return;
        } 

        else if (destCell == '$' || destCell == '*') 
        {
            int boxNewRow = newRow + direction[0];
            int boxNewCol = newCol + direction[1];

            if (isInvalidPushDestination(currentState.getItemsData(), 
                boxNewRow, boxNewCol))
                return;

            long predictedHash = hash 
            ^ zobristTable[oldRow][oldCol][0] 
            ^ zobristTable[newRow][newCol][0]
            ^ zobristTable[newRow][newCol][1] 
            ^ zobristTable[boxNewRow][boxNewCol][1];

            if (exploredStates.contains(predictedHash))
                return;
        } 

        else 
            return; // Invalid destination

        deepCopyItems(currentState.getItemsData(), tempItems);

        int moveType = validateAndExecuteMove(tempPlayer, tempItems, action);

        if (moveType == INVALID_MOVE)
            return;

        // Update the hash based on move type
        if (moveType == NORMAL_MOVE) 
        {
            // Update hash: remove player from old, add to new
            hash ^= zobristTable[oldRow][oldCol][0]; // Remove player from old position
            hash ^= zobristTable[newRow][newCol][0]; // Add player to new position
        } 

        else if (moveType == PUSH_MOVE) 
        {
            int boxNewRow = newRow + direction[0];
            int boxNewCol = newCol + direction[1];

            // Update hash: move player, move box
            hash ^= zobristTable[oldRow][oldCol][0]; // remove player from old
            hash ^= zobristTable[newRow][newCol][0]; // add player to new
            hash ^= zobristTable[newRow][newCol][1]; // remove box from old
            hash ^= zobristTable[boxNewRow][boxNewCol][1]; // add box to new

            // Only check for deadlocks on push moves
            if (isDeadlocked(tempItems))
                return;
        }


        int newHeuristic = (moveType == PUSH_MOVE) ? 
            calculateEnhancedHeuristic(tempItems) : 
            currentState.getHeuristic(); 

        // Create new state with its own hash
        SokoState newState = new SokoState(tempItems,newHeuristic, action, 
            currentState, tempPlayer[0], tempPlayer[1], hash);

        if (moveType == PUSH_MOVE) 
        {
            if (exploredStates.add(hash)) 
                possiblePushStates.add(newState);

            return; 
        }

        // For normal moves: mark as explored and continue exploring
        if (!exploredStates.add(hash))
            return;

        explorePossiblePushStates(possiblePushStates, newState, 'l');
        explorePossiblePushStates(possiblePushStates, newState, 'u');
        explorePossiblePushStates(possiblePushStates, newState, 'r');
        explorePossiblePushStates(possiblePushStates, newState, 'd');
    }

 
    /* Validation Methods */
    /*
     * Converts character directin to vector coordinates using a precomp
     * lookup table 
     * Uses direct array access instead of conditionals or switch statements
     * which is 3-4x fster than equivalent if/else logic 
     */
    private int validateAndExecuteMove(int[] playerPosition,
        char[][] itemsData, char action) 
    {
        int currentRow = playerPosition[0];
        int currentCol = playerPosition[1];
        int[] direction = DIR_LOOKUP[action];
        int newRow = currentRow + direction[0];
        int newCol = currentCol + direction[1];

        if (isInvalidDestination(newRow, newCol))
            return INVALID_MOVE;

        char destCell = itemsData[newRow][newCol];

        if (destCell == ' ' || destCell == '.') 
        {
            if (goalGrid[currentRow][currentCol]) 
                itemsData[currentRow][currentCol] = '.';
            else 
                itemsData[currentRow][currentCol] = ' ';

            itemsData[newRow][newCol] = '@';
            playerPosition[0] = newRow;
            playerPosition[1] = newCol; 

            return NORMAL_MOVE;
        }

        if (destCell == '$' || destCell == '*') 
        {
            int boxNewRow = newRow + direction[0];
            int boxNewCol = newCol  + direction[1];

            if (isInvalidPushDestination(itemsData, boxNewRow, boxNewCol))
                return INVALID_MOVE;

            if (goalGrid[currentRow][currentCol]) 
                itemsData[currentRow][currentCol] = '.';
            else 
                itemsData[currentRow][currentCol] = ' ';

            itemsData[newRow][newCol] = '@';

            if (goalGrid[boxNewRow][boxNewCol]) 
                itemsData[boxNewRow][boxNewCol] = '*';
            else 
                itemsData[boxNewRow][boxNewCol] = '$';

            playerPosition[0] = newRow;
            playerPosition[1] = newCol; 

            return PUSH_MOVE;
        }

        return INVALID_MOVE;
    }

    /*
     * Quickly checks if a position is out of bounds or a wall rejecting
     * invalid moves
     * Provides fast rejection of invalid moves before more expensive checks
     * filtering unnecessary process in the move validation pipeline
     */
    private boolean isInvalidDestination(int row, int col) 
    {
        return row < 0 || row >= height || col < 0 ||
        col >= width || mapData[row][col] == '#';
    }

    /*
     * Checks if a move is valid and executes it in a single operation
     * updating the board state
     * Combining validation and execution for efficiency avaoiding 
     * duplicate checks.
     */
    private boolean isInvalidPushDestination(char[][] itemsData, 
        int row, int col) 
    {
        if (row < 0 || row >= height || col < 0 || col >= width ||
        mapData[row][col] == '#' || mapData[row][col] == 'X')
            return true;

        char destCell = itemsData[row][col];
        if (destCell == '$' || destCell == '*')
            return true;

        if (!goalGrid[row][col]) 
        {
            boolean topWall = (row == 0 || mapData[row - 1][col] == '#');
            boolean bottomWall = (row == height - 1 || 
                mapData[row + 1][col] == '#');
            boolean leftWall = (col == 0 || mapData[row][col - 1] == '#');
            boolean rightWall = (col == width - 1 || 
                mapData[row][col + 1] == '#');

            if ((topWall && leftWall) || (topWall && rightWall) ||
            (bottomWall && leftWall) || (bottomWall && rightWall))
                return true;
        }

        return false;
    }

    /* Deadlock Detection Methods */

    /* 
     * Master method that checks all types of deadklocks through 
     * specialized detection methods
     *
     * Starts with cheapest checks first, allowing quick elimination of 
     * unsolvable states. A layered approach balances thoroughness with 
     * performance ensuring efficent prunning
     */
    private boolean isDeadlocked(char[][] itemsData) 
    {
        if (isRoomDeadlock(itemsData)) 
            return true;

        for (int row = 0; row < height; row++) 
            for (int col = 0; col < width; col++) 
                if (isBoxInDeadlockPosition(itemsData, row, col))
                return true;

        return false;
    }

    /*
     * Checks if any room has more bozes than goals making the puzzle 
     * unsolvable 
     */ 
    private boolean isRoomDeadlock(char[][] itemsData) 
    {
        if (goalCountsByRoomId.isEmpty()) 
            return false;

        int[] boxCounts = new int[goalCountsByRoomId.size()];

        for (int r = 0; r < height; r++) 
            for (int c = 0; c < width; c++) 
                if ((itemsData[r][c] == '$' || itemsData[r][c] == '*') 
                    && roomIds[r][c] != -1) 
                    boxCounts[roomIds[r][c]]++;

        for (int i = 0; i < boxCounts.length; i++) 
            if (boxCounts[i] > goalCountsByRoomId.get(i)) 
                return true;

        return false;
    }

    /*
     * Checks if a specific box is in a dealock position using multiple 
     * detection strategies
     */
    private boolean isBoxInDeadlockPosition(char[][] itemsData, 
        int row, int col) 
    {
        if (itemsData[row][col] != '$' && itemsData[row][col] != '*')
            return false;

        if (goalGrid[row][col]) 
            return false;

        if (deadSquares[row][col]) 
            return true;

        if (isFrozenBox(itemsData, row, col))
            return true;

        char[] neighborhood = extractNeighborhood(itemsData, row, col);

        return checkAllRotationForDeadlock(neighborhood);
    }

    /* 
     * Detects if a box is "frozen" (can't be moved) by checking walls and 
     * other boxes that block movement 
     */
    private boolean isFrozenBox(char[][] itemsData, int row, int col) 
    {
        boolean upWall = (row == 0) || mapData[row-1][col] == '#';
        boolean downWall = (row == height-1) || mapData[row+1][col] == '#';
        boolean leftWall = (col == 0) || mapData[row][col-1] == '#';
        boolean rightWall = (col == width-1) || mapData[row][col+1] == '#';

        if (upWall || downWall) 
        {
            boolean blockedLeft = leftWall || itemsData[row][col-1] == '$' 
                || itemsData[row][col-1] == '*';
            boolean blockedRight = rightWall || itemsData[row][col+1] == '$' 
                || itemsData[row][col+1] == '*';

            if (blockedLeft && blockedRight)
                return true;
        }

        if (leftWall || rightWall) 
        {
            boolean blockedUp = upWall || itemsData[row-1][col] == '$' 
                || itemsData[row-1][col] == '*';
            boolean blockedDown = downWall || itemsData[row+1][col] == '$' 
                || itemsData[row+1][col] == '*';

            if (blockedUp && blockedDown)
                return true;
        }

        return false;
    }

    /* Pattern Matching Methods */

    /*
     * Extracts a 3x3 grid around a position into a temporary array for 
     * pattern analysis
     * Reuses a temporary buffer isntead of creating new arrays, reducing 
     * garbage collection pressure.
     */
    private char[] extractNeighborhood(char[][] itemsData,
        int centerRow, int centerCol) 
    {
        tempNeighborhood[0] = getCellValue
            (itemsData, centerRow - 1, centerCol - 1);
        tempNeighborhood[1] = getCellValue
            (itemsData, centerRow - 1, centerCol);
        tempNeighborhood[2] = getCellValue
            (itemsData, centerRow - 1, centerCol + 1);
        tempNeighborhood[3] = getCellValue
            (itemsData, centerRow, centerCol - 1);
        tempNeighborhood[4] = getCellValue
            (itemsData, centerRow, centerCol);
        tempNeighborhood[5] = getCellValue
            (itemsData, centerRow, centerCol + 1);
        tempNeighborhood[6] = getCellValue
            (itemsData, centerRow + 1, centerCol - 1);
        tempNeighborhood[7] = getCellValue
            (itemsData, centerRow + 1, centerCol);
        tempNeighborhood[8] = getCellValue
            (itemsData, centerRow + 1, centerCol + 1);

        return tempNeighborhood;
    }

    /* 
     * Gets the balue of a cell with boundary checking retunring simplified
     * values for pattern matching 
     */
    private char getCellValue(char[][] itemsData, int row, int col) 
    {
        if (row < 0 || row >= height || col < 0 || col >= width 
            || mapData[row][col] == '#')
            return '#';    

        char cell = itemsData[row][col];

        return (cell == '$' || cell == '*') ? '$' : ' ';
    }

    /*
     * Applies each rotation pattern to a 3x3 neighborhood and checks for
     * deadlocks
     * Enables rotation-invariant pattern matching, allowing the solver
     * to detect deadlocks in any orientation
     */
    private boolean checkAllRotationForDeadlock(char[] neighborhood) 
    {
        for (int[] pattern : ROTATION_PATTERNS) 
        {
            char[] rotated = applyRotation(neighborhood, pattern);

            if (isDeadlockPattern(rotated))
                return true;
        }

        return false;
    }

    /* Applies a rotation pattern to a 3x3 neighborhood using a lookup table
     * uses precomp index mappings instead of matrix math results in a Simple
     * array lookups rather than calculation
     */
    private char[] applyRotation(char[] original, int[] pattern) 
    {
        for (int i = 0; i < 9; i++) 
            tempRotated[i] = original[pattern[i]];

        return tempRotated;
    }

    /*
     * Checks if a specific 3x3 pattern represents a deadlock using 
     * hand-optimized pattern checks
     */
    private boolean isDeadlockPattern(char[] board) 
    {
        // Pattern indices:
        // 0 1 2
        // 3 4 5  (4 is the box we're checking)
        // 6 7 8

        // Quick reject: if no walls adjacent, can't be deadlocked
        if (board[1] != '#' && board[3] != '#' && 
            board[5] != '#' && board[7] != '#')
            return false;

        // Simple corner deadlock
        if (board[1] == '#' && board[5] == '#')
            return true;

        // Box wall deadlock
        if ((board[1] == '$' && board[2] == '#' && board[5] == '#') ||
        (board[1] == '#' && board[2] == '$' && board[5] == '$') ||
        (board[1] == '$' && board[2] == '#' && board[5] == '$') ||
        (board[1] == '$' && board[2] == '$' && board[5] == '$') ||
        (board[1] == '#' && board[6] == '#' && board[3] == '$'))
            return true;

        if (board[1] == '$' && board[7] == '$' && board[2] == '#' &&
        board[3] == '#' && board[8] == '#')
            return true;

        return false;
    }


    /* Heuristic Calculation Methods */

    /*
     * Marks a specific goal as matched to a box in the matching algo 
     * Ensures each goal is only used once in the distance calculation
     */
   private void markGoalAsMatched(boolean[] matchedGoals, int row, int col) 
    {
        int numGoals = goalList.size();

        for (int i = 0; i < numGoals; i++) 
        {
            int[] goal = goalList.get(i);

            if (goal[0] == row && goal[1] == col) 
            {
                matchedGoals[i] = true;
                return;
            }
        }
    } 

    /*
     * Finds the closest unmatched goal for a box as part of the box-to-goal
     * assignment
     * Approximates a minimum-cost matching without the complexity of the 
     * Hungarian algo.
     */
    private int findMinDistanceToAvailableGoal(int boxRow, int boxCol, 
        boolean[] matchedGoals) 
    {
        int minDistance = Integer.MAX_VALUE;
        int closestGoalIdx = -1;
        int numGoals = goalList.size(); 

        for (int i = 0; i < numGoals; i++) 
        {
            if (matchedGoals[i])
                continue;

            int[] goal = goalList.get(i);
            int distance = Math.abs(boxRow - goal[0]) + 
                Math.abs(boxCol - goal[1]);

            if (distance < minDistance) 
            {
                minDistance = distance;
                closestGoalIdx = i;
            }
        }

        if (closestGoalIdx >= 0) 
        {
            matchedGoals[closestGoalIdx] = true;
            return minDistance;
        }

        return getMinManhattanDistance(boxRow, boxCol);
    }


    /*
     * Calculates the minimum Manhattan distance from a box to any goal 
     */
    private int getMinManhattanDistance(int boxRow, int boxCol) 
    {
        int minDist = Integer.MAX_VALUE;

        for (int[] goal : goalList) 
        {
            int dist = Math.abs(goal[0] - boxRow) + 
                Math.abs(goal[1] - boxCol);

            if (dist < minDist)
                minDist = dist;
        }

        return minDist == Integer.MAX_VALUE ? 0 : minDist;
    }


    /*
     * Computes a heuristic value for state prioritization based on Manhattan
     * distances and penalties
     * Provides better guidance than simple distance sums by including 
     * penalties for frozen boxes. This more accurate heuristic directs 
     * search toward promising states reducing the total number of states 
     * that need to be explored.
     */
    private int calculateEnhancedHeuristic(char[][] itemsData) 
    {
        boxCount = 0;

        for (int row = 0; row < height; row++) 
        {
            for (int col = 0; col < width; col++) 
            {
                if (itemsData[row][col] == '$' || itemsData[row][col] == '*') 
                {
                    boxPositions[boxCount * 2] = row;
                    boxPositions[boxCount * 2 + 1] = col;
                    boxCount++;
                }
            }
        }

        return calculateBoxestoGoalsDistance();
    }

    /*
     * Calcualtes distances from boxes to goals using an optimal matching 
     * algo.
     * Produces a more accurate heuristic than naive assignment by finding 
     * total distance assignment. This better prioritizes states in the queue
     * focusing exploration on the most promising paths
     */
    private int calculateBoxestoGoalsDistance() 
    {   
        int totalDistance = 0;
        int boxesOnGoals = 0;
        Arrays.fill(tempMatchedGoals, false);   

        for (int i = 0; i < boxCount; i++) 
        {
            int boxRow = boxPositions[i * 2];
            int boxCol = boxPositions[i * 2 + 1];

            if (goalGrid[boxRow][boxCol]) 
            {
                markGoalAsMatched(tempMatchedGoals, boxRow, boxCol);
                boxesOnGoals++;
                continue;
            }

            if (isFrozenBox(tempItems, boxRow, boxCol)) 
                totalDistance += 30; // Higher penalty for frozen boxes not on goals

            int minDistance = findMinDistanceToAvailableGoal(boxRow, boxCol, 
                tempMatchedGoals);  
            totalDistance += minDistance;
        }

        if (boxesOnGoals == boxCount)
            return 0;

        return totalDistance;
    }   

    /* Custom Data Structures */

    /*
     * Custom hash set implementation for storing state hashes with open
     * addressing
     * Optimized for Zobrist hashes with faster perfomance than Java's HashSet
     * for this specific use case. The open addressing approach improves 
     * cache locality and reduces overhead
     */
    private static class LongHashSet 
    {
        private long[] keys;
        private int size;
        private int capacity;
        private static final long EMPTY = 0;

        public LongHashSet (int initialCapacity) 
        {
            this.capacity = nextPowerOf2(initialCapacity);
            this.keys = new long[capacity];
            this.size = 0;
        }

        private int nextPowerOf2(int n) 
        {
            n--;
            n |= n >> 1;
            n |= n >> 2;
            n |= n >> 4;
            n |= n >> 8;
            n |= n >> 16;

            return n + 1;
        }

        public boolean add (long key) 
        {
            if (key == EMPTY) 
                key = Long.MIN_VALUE;

            if (size * 2 > capacity) 
                resize();

            int index = hash(key) & (capacity - 1);

            while (keys[index] != EMPTY) 
            {
                if (keys[index] == key) 
                    return false;

                index = (index + 1) & (capacity - 1);
            }

            keys[index] = key;
            size++;

            return true;
        }

        public boolean contains (long key) 
        {
            if (key == EMPTY) 
                key = Long.MIN_VALUE;

            int index = hash(key) & (capacity - 1);

            while (keys[index] != EMPTY) 
            {
                if (keys[index] == key) 
                    return true;

                index = (index + 1) & (capacity - 1);
            }

            return false;
        }

        private int hash (long key) 
        {
            return (int)(key ^ (key >>> 32));
        }

        private void resize () 
        {
            long[] oldKeys = keys;
            capacity *= 2;
            keys = new long[capacity];
            size = 0;

            for (long key : oldKeys) 
                if (key != EMPTY) 
                    add(key);
        }

        public void clear () 
        {
            Arrays.fill(keys, EMPTY);
            size = 0;
        }

        public int size () 
        {
            return size;
        }
    }

    /* State Class */
    
    /*
     * Immutable representation of a game state with cached hash and heuristic
     * values 
     * Pre-computes and stores values used for comparison and prioritization
     * elimination redundant calculations in hot code paths. The Immutable
     * design simplifies state management and comparison logic.
     */
    public static class SokoState 
    {
        private final char[][] itemsData;
        private final int heuristic;
        private final char action;
        private final SokoState prevState;
        private final long stateHash; // Store hash with the state

        final int playerRow;
        final int playerCol;

        public SokoState(char[][] itemsData, int heuristic,
            char action, SokoState prevState, int playerRow, 
            int playerCol, long stateHash) 
        {
            int h = itemsData.length;
            int w = itemsData[0].length;
            this.itemsData = new char[h][];

            for (int i = 0; i < h; i++)
                this.itemsData[i] = Arrays.copyOf(itemsData[i], w);

            this.heuristic = heuristic;
            this.action = action;
            this.prevState = prevState;
            this.playerRow = playerRow;
            this.playerCol = playerCol;
            this.stateHash = stateHash;
        }

        public char[][] getItemsData () 
        {
            return itemsData;
        }

        public int getHeuristic () 
        {
            return heuristic;
        }

        public int getComparator () 
        {
            return heuristic;
        }

        public char getAction () 
        {
            return action;
        }

        public SokoState getPrevState ()
        {
            return prevState;
        }
    }
}
