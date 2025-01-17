//
// Solves tic-tac-toe for the 3 unique first moves. Do it 10,000 times so timing is accurate.
// Prove there can be no winner.
// board:
//   0 1 2
//   3 4 5
//   6 7 8
//
// build using: rustc -O ttt.rs
// build for RVOS risc-v emulator: rustc -O -C target-feature=+crt-static ttt.rs
//

use std::thread;
use std::time::Instant;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::env;
use std::process;

static MINMAXMOVES: AtomicUsize = AtomicUsize::new( 0 );

mod board
{
    use std::fmt;

    #[derive(PartialEq, Copy, Clone)]
    pub enum Piece { Blank, X, O, }

    impl Default for Piece
    {
        fn default() -> Self { Piece::Blank }
    }

    impl fmt::Display for Piece
    {
        fn fmt( &self, f: &mut fmt::Formatter ) -> fmt::Result
        {
            match *self
            {
                Piece::Blank => write!( f, " " ),
                Piece::X     => write!( f, "X" ),
                Piece::O     => write!( f, "O" ),
            }
        }
    }

    pub type Board = [ Piece; 9 ];
}

const AB_PRUNE: bool = true;
const WIN_LOSE_PRUNE: bool = true;
const ENABLE_DEBUG: bool = false;
const SCORE_WIN:  i32 = 6;
const SCORE_TIE:  i32 = 5;
const SCORE_LOSE: i32 = 4;
const SCORE_MAX:  i32 = 9;
const SCORE_MIN:  i32 = 2;

#[allow(dead_code)]
fn look_for_winner( b: & board::Board ) -> board::Piece
{
    let mut p: board::Piece = b[0];
    if board::Piece::Blank != p
    {
        if p == b[1] && p == b[2] {
            return p;
        }
    
        if p == b[3] && p == b[6] {
            return p;
        }
    }
    
    p = b[3];
    if board::Piece::Blank != p && p == b[4] && p == b[5] {
        return p;
    }

    p = b[6];
    if board::Piece::Blank != p && p == b[7] && p == b[8] {
        return p;
    }
    
    p = b[1];
    if board::Piece::Blank != p && p == b[4] && p == b[7] {
        return p;
    }

    p = b[2];
    if board::Piece::Blank != p && p == b[5] && p == b[8] {
        return p;
    }

    p = b[4];
    if board::Piece::Blank != p
    {
        if p == b[0] && p == b[8] {
            return p;
        }

        if p == b[2] && p == b[6] {
            return p;
        }
    }

    return board::Piece::Blank;
} //look_for_winner

fn pos0_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 0 ];

    if ( x == b[1] && x == b[2] ) ||
       ( x == b[3] && x == b[6] ) ||
       ( x == b[4] && x == b[8] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos1_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 1 ];

    if ( x == b[0] && x == b[2] ) ||
       ( x == b[4] && x == b[7] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos2_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 2 ];

    if ( x == b[0] && x == b[1] ) ||
       ( x == b[5] && x == b[8] ) ||
       ( x == b[4] && x == b[6] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos3_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 3 ];

    if ( x == b[4] && x == b[5] ) ||
       ( x == b[0] && x == b[6] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos4_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 4 ];

    if ( x == b[0] && x == b[8] ) ||
       ( x == b[2] && x == b[6] ) ||
       ( x == b[1] && x == b[7] ) ||
       ( x == b[3] && x == b[5] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos5_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 5 ];

    if ( x == b[3] && x == b[4] ) ||
       ( x == b[2] && x == b[8] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos6_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 6 ];

    if ( x == b[7] && x == b[8] ) ||
       ( x == b[0] && x == b[3] ) ||
       ( x == b[4] && x == b[2] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos7_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 7 ];

    if ( x == b[6] && x == b[8] ) ||
       ( x == b[1] && x == b[4] ) {
        return x;
    }

    return board::Piece::Blank;
}

fn pos8_func( b: & board::Board ) -> board::Piece
{
    let x: board::Piece = b[ 8 ];

    if ( x == b[6] && x == b[7] ) ||
       ( x == b[2] && x == b[5] ) ||
       ( x == b[0] && x == b[4] ) {
        return x;
    }

    return board::Piece::Blank;
}

type PosFunc = fn( & board::Board ) -> board::Piece;
static POS_FUNCS: [ PosFunc; 9 ] = [ pos0_func, pos1_func, pos2_func, pos3_func, pos4_func, pos5_func, pos6_func, pos7_func, pos8_func ];

fn min_max( b: &mut board::Board, mut alpha: i32, mut beta: i32, depth: i32, mov: usize, move_count : &mut usize ) -> i32
{
    *move_count += 1;

    if depth >= 4
    {
        //let p: board::Piece = look_for_winner( & b );
        let p: board::Piece = POS_FUNCS[ mov ]( &b );

        if board::Piece::Blank != p {
            if board::Piece::X == p {
                return SCORE_WIN;
            }
            return SCORE_LOSE;
        }

        if 8 == depth {
            return SCORE_TIE;
        }
    }

    let mut value: i32;
    let piece_move: board::Piece;

    if 0 != ( depth & 1 ) { // maximize
        value = SCORE_MIN;
        piece_move = board::Piece::X;
    }
    else {
        value = SCORE_MAX;
        piece_move = board::Piece::O;
    }

    for x in 0..=8
    {
        if board::Piece::Blank == b[ x ]
        {
            b[ x ] = piece_move;
            let score: i32 = min_max( b, alpha, beta, depth + 1, x, move_count );
            b[ x ] = board::Piece::Blank;

            if 0 != ( depth & 1 ) { // maximize
                if WIN_LOSE_PRUNE && SCORE_WIN == score {
                    return SCORE_WIN;
                }

                if score > value {
                    value = score;

                    if AB_PRUNE {
                        if value >= beta {
                            return value;
                        }
                        if value > alpha {
                            alpha = value;
                        }
                    }
                }
            }
            else {
                if WIN_LOSE_PRUNE && SCORE_LOSE == score {
                    return SCORE_LOSE;
                }

                if score < value {
                    value = score;

                    if AB_PRUNE {
                        if value <= alpha {
                            return value;
                        }
                        if value < beta {
                            beta = value;
                        }
                    }
                }
            }
        }
    }

    return value;
} //min_max

fn run_board( mov: usize, iterations: i32 )
{
    let mut b: board::Board = Default::default();
    b[ mov ] = board::Piece::X;
    let mut move_count: usize = 0;
    
    for _l in 0..iterations
    {
        let score = min_max( &mut b, SCORE_MIN, SCORE_MAX, 0, mov, & mut move_count );
        if ENABLE_DEBUG && SCORE_TIE != score {
            println!( "score is {}", score );
        }
    }

    MINMAXMOVES.fetch_add( move_count, Ordering::SeqCst );
} //run_board

fn main()
{
    const DEFAULT_ITERATIONS: i32 = 1;
    let mut iterations: i32 = 0;

    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        iterations = args[1].parse().unwrap();
    }

    if 0 == iterations {
        iterations = DEFAULT_ITERATIONS;
    }

    if ENABLE_DEBUG {
        run_board( 0, 1 );
        let calls = MINMAXMOVES.load( Ordering::SeqCst );
        println!( "calls to min_max: {}", calls );
    
        if AB_PRUNE && WIN_LOSE_PRUNE && 1903 != calls {
            println!( "unexpected # of calls to minmax" );
        }

        process::exit( 0 );
    }

    let in_rvos: bool;
    match env::var( "OS" ) {
        Ok(val) => in_rvos = val == "RVOS",
        Err(_e) => in_rvos = false,
    }

    let in_armos: bool;
    match env::var( "OS" ) {
        Ok(val) => in_armos = val == "ARMOS",
        Err(_e) => in_armos = false,
    }

    if !in_rvos && !in_armos {     // rvos just supports one thread
        MINMAXMOVES.store( 0, Ordering::SeqCst );
        let parallel_start = Instant::now();
    
        // Parallel run
    
        let thread0 = thread::spawn( move ||
        {
            run_board( 0, iterations );
        });
    
        let thread1 = thread::spawn( move ||
        {
            run_board( 1, iterations );
        });

        // run position 4 on this thread

        run_board( 4, iterations );
    
        thread0.join().unwrap();
        thread1.join().unwrap();
    
        let parallel_end = Instant::now();
        println!( "parallel runtime: {:?}", parallel_end.checked_duration_since( parallel_start ).unwrap() );
        let parallel_moves = MINMAXMOVES.load( Ordering::SeqCst );
        println!( "moves:            {}", parallel_moves );
    }

    // Serial run

    MINMAXMOVES.store( 0, Ordering::SeqCst );
    let serial_start = Instant::now();

    run_board( 0, iterations );
    run_board( 1, iterations );
    run_board( 4, iterations );

    let serial_end = Instant::now();
    println!( "serial runtime:   {:?}", serial_end.checked_duration_since( serial_start ).unwrap() );
    let serial_moves = MINMAXMOVES.load( Ordering::SeqCst );
    println!( "moves:            {}", serial_moves );
    println!( "iterations:       {}", iterations );
} //main

