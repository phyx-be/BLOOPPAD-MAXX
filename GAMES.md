# BloopPad Maxx games manual

The game menu shows every mode as a colored button: the original games on the
**top row**, the newer games on the **bottom row**. Press a button once to
select it (it blinks and its name scrolls by in the middle rows), press it
again to start it. After a game ends its button stays selected, so one press
plays it again.

Games that support more than one player all use the same selection screen on
the first row:

* press the **red** or the **blue** button: one player with that color,
  the computer plays the other color
* press **both simultaneously**: two players on one BloopPad Maxx

Games against the computer then ask for the difficulty on the first row:
**green** = easy, **yellow** = normal, **red** = hard.

## Top row

### Connect 4 (yellow)
Press any button in a column to drop a piece in that column. With two players
red starts; one player always starts and the computer answers. The first four
in a row (horizontal, vertical or diagonal) wins.

### Masher (magenta)
Two halves, one rope: red owns the columns left of the rope, blue the columns
right of it. Every press on a button in your own half pulls the rope towards
the other side, idle hands let it slide back to the middle. Pull the rope all
the way to the other edge to win.

### Tug of war (cyan)
Every round, after a random wait, a white light appears in each half. The
first to press the light in their own half wins the round and pulls the rope
one column towards the other side. Pressing before the lights appear or
pressing the wrong button loses the round. Pull the rope all the way to the
other edge to win.

### Rainbow (green)
Not really a game: the buttons on the first row pick one of the 8 LED
animations (rainbow wave, plasma, fire, matrix rain, sparkles, ...).

### Game of life (orange)
Every button is a cell. Tap a button to toggle that cell (also while
running), let it evolve and watch Conway's game of life on 64 chunky pixels.
A cell with 3 living neighbors is born, a living cell with 2 or 3 living
neighbors survives, every other cell dies. The board does not wrap around
and a new generation follows every 0.3 s.

### Tetris (red)
The whole board is the playing field, the buttons of the bottom row are the
controls: col 0 move left, col 1 turn counterclockwise, col 2..5 move down,
col 6 turn clockwise, col 7 move right.

### Battleships (blue)
Sink the fleet of the other player first. Place your ships, then take turns
firing at the other half of the board.

### Othello (spring green)
Reversi on the full board. Press one of the dim cells of your color to place
a disc and flip every opponent disc you trap between your discs. When a
player cannot move, they pass; the game ends when neither player can move or
the board is full. Most discs wins.

* against the computer: easy plays random legal moves, normal maximizes the
  number of flips, hard also weighs corner/edge positions and your best
  counter-move.

## Bottom row

### Minesweeper (pink)
Press a cell to reveal it, **press and hold** a cell (±0.4 s) to plant or
remove a flag. The first reveal is always safe; a revealed number shows how
many of the 8 neighbors have a mine, using the classic colors (1 blue, 2
green, 3 red, ...). Revealing an empty cell opens its neighbors as well.
Clear every cell without a mine to win. More mines on higher difficulty
(8 / 12 / 16).

### Lights out (lavender)
Pressing a button toggles it and its four neighbors. Turn all the LEDs off
to win. The puzzle is scrambled from the solved state, so it always has a
solution; higher difficulty scrambles with more presses (3 / 6 / 10). The
result screen shows how many moves you needed.

### Mastermind (purple)
Crack a secret code of 4 pegs (6 colors) in 7 guesses. Build a guess on the
bottom row: press a peg slot (columns 0..3) to cycle its color, press the
green button (column 7) to submit. Every stored guess shows its feedback on
the right (columns 4..7): **bright white** = right color in the right place,
**dim white** = right color in the wrong place. With two players the second
player secretly sets the code first; with one player the result screen
reveals the code as digits (colors numbered 1..6).

### Paddle duel (salmon)
The ball crosses the board between the two edge columns. Hold the button on
your edge where the ball arrives to bounce it back - hold two adjacent edge
buttons for a bigger paddle (thanks to the n-key rollover of the matrix).
Where your paddle catches the ball sets the bounce angle, and every hit
speeds the rally up. First to 5 points wins; the score bars and the blinking
serve side are shown between points.

### Whack-a-mole (lime)
Moles pop up in your half in your color - smack them before they hide again.
Pressing an empty button in your half costs a point. The round lasts 30
seconds and the pace keeps increasing: moles live shorter and appear faster.
Solo mode uses the whole board as a score attack.

### Rhythm tap (amber)
Lights race across the rows towards the **green target column on the left**.
Press the target button of that row the moment the light arrives: green flash
= perfect (+100), yellow flash = good (+50), red flash = it got away. The
higher the difficulty, the faster the lights and the shorter the gaps.

### Finger twister (azure)
Buttons light up that you must hold - **all of them at the same time**, while
new ones keep appearing. A newly lit button (it blinks) may be grabbed at
leisure, but a button you were holding must not be let go for more than a
split second. Hold 4 (easy), 6 (normal) or 8 (hard) buttons at once to win;
the score shows how far you got.

### MIDI (white)
Turns the pad into a USB-MIDI controller: buttons are sent as Control Change
messages and the host sets the LEDs - see the [README](README.md#midi-messages).
This mode never returns to the menu on its own; send the bootloader SysEx or
replug to leave it.
