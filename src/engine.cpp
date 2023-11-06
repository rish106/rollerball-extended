#include <algorithm>
#include <chrono>
#include <random>
#include <iostream>
#include <thread>
#include <climits>
#include <unordered_map>
#include <unordered_set>

using namespace std;

#include "board.hpp"
#include "engine.hpp"
#include "butils.hpp"

int MIN_SEARCH_DEPTH = 2;
int MAX_SEARCH_DEPTH = 6;
const int QUIESCENCE_DEPTH = 2;

const int PAWN_WEIGHT = 150;
const int ROOK_WEIGHT = 600;
const int BISHOP_WEIGHT = 400;
const int KING_WEIGHT = 1500;
const int CHECK_WEIGHT = 99;
const int STALEMATE_WEIGHT = 1000;
const int REPETITION_WEIGHT = 1000;
// const int RING_WEIGHT = 20;

const int ATTACKING_FACTOR = 6;
const int DEFENDING_FACTOR = 4;

int nodes_visited;
int curr_player = -1;

int PLAYER_WEIGHTS[6] = {ROOK_WEIGHT, ROOK_WEIGHT, KING_WEIGHT, BISHOP_WEIGHT, PAWN_WEIGHT, PAWN_WEIGHT};
int OPPONENT_WEIGHTS[6] = {ROOK_WEIGHT, ROOK_WEIGHT, KING_WEIGHT, BISHOP_WEIGHT, PAWN_WEIGHT, PAWN_WEIGHT};

unordered_map<U8, int> quadrants;
U8 quad_points[4] = {pos(1, 1), pos(1, 5), pos(5, 5), pos(5, 1)};

unordered_map<string, int> previous_board_occurences;

struct Evaluation {
    int piece_weight    = 0;
    int promo           = 0;
    int check           = 0;
    int king_distance   = 0;
    int depth           = 0;
    int attack          = 0;
    // int ring_weight     = 0;
    int total           = 0;
    vector<U16> moves;

    void reset() {
        piece_weight    = 0;
        promo           = 0;
        attack          = 0;
        check           = 0;
        king_distance   = 0;
        // ring_weight     = 0;
        total           = 0;
    }

    void update_total() {
        total = 0;
        total += piece_weight;
        total += attack;
        total += promo;
        total += check;
        total += king_distance;
        // total += ring_weight;
    }

    void print() {
        cout << "piece weight   " << piece_weight << '\n';
        cout << "promo          " << promo << '\n';
        cout << "attack         " << attack << '\n';
        cout << "check          " << check << '\n';
        cout << "depth          " << depth << '\n';
        cout << "king distance  " << king_distance << '\n';
        // cout << "ring weight    " << ring_weight << '\n';
        cout << "total          " << total << '\n';
    }
};

void flip_player(Board& b) {
    b.data.player_to_play = (PlayerColor)(b.data.player_to_play ^ (WHITE | BLACK));
}

void init_quadrant_map() {
    quadrants[pos(0, 0)] = quadrants[pos(5, 0)] = 0;
    for (int x = 1; x <= 4; x++) {
        for (int y = 0; y <= 1; y++) {
            quadrants[pos(x, y)] = 0;
        }
    }
    quadrants[pos(0, 1)] = quadrants[pos(0, 6)] = 1;
    for (int x = 0; x <= 1; x++) {
        for (int y = 2; y <= 5; y++) {
            quadrants[pos(x, y)] = 1;
        }
    }
    quadrants[pos(1, 6)] = quadrants[pos(6, 6)] = 2;
    for (int x = 2; x <= 5; x++) {
        for (int y = 5; y <= 6; y++) {
            quadrants[pos(x, y)] = 2;
        }
    }
    quadrants[pos(6, 0)] = quadrants[pos(6, 5)] = 3;
    for (int x = 5; x <= 6; x++) {
        for (int y = 1; y <= 4; y++) {
            quadrants[pos(x, y)] = 3;
        }
    }
}

int get_distance(U8 initial_pos, U8 final_pos) {
    int distance = 0;
    U8 distance_x, distance_y;
    U8 initial_quad = quadrants[initial_pos];
    U8 final_quad = quadrants[final_pos];
    U8 initial_x = getx(initial_pos);
    U8 initial_y = gety(initial_pos);
    U8 final_x = getx(final_pos);
    U8 final_y = gety(final_pos);
    if (initial_quad == final_quad) {
        U8 initial_coordinate = (initial_quad % 2 == 0 ? initial_x : initial_y);
        U8 final_coordinate = (initial_quad % 2 == 0 ? final_x : final_y);
        if ((((initial_quad == 1 || initial_quad == 2) && initial_coordinate >= final_coordinate) || ((initial_quad == 0 || initial_quad == 3) && initial_coordinate <= final_coordinate))) {
            distance += 12;
            U8 prev_quad = (final_quad + 3) % 4;
            U8 special_point = quad_points[prev_quad];
            distance_x = abs(final_x - getx(special_point));
            distance_y = abs(final_y - gety(special_point));
            distance += max(distance_x, distance_y);
            special_point = quad_points[initial_quad];
            distance_x = abs(initial_x - getx(special_point));
            distance_y = abs(initial_y - gety(special_point));
            distance += max(distance_x, distance_y);
        } else {
            distance += abs(final_coordinate - initial_coordinate);
        }
    } else {
        U8 quad_diff = (final_quad > initial_quad ? final_quad - initial_quad : 4 + final_quad - initial_quad);
        distance += 4 * (quad_diff - 1);
        U8 prev_quad = (final_quad + 3) % 4;
        U8 special_point = quad_points[prev_quad];
        distance_x = abs(final_x - getx(special_point));
        distance_y = abs(final_y - gety(special_point));
        distance += max(distance_x, distance_y);
        special_point = quad_points[initial_quad];
        distance_x = abs(initial_x - getx(special_point));
        distance_y = abs(initial_y - gety(special_point));
        distance += max(distance_x, distance_y);
    }
    return distance;
}

int get_rook_distance(U8 rook_pos, U8 final_pos) {
    int rook_quad = quadrants[rook_pos];
    int final_quad = quadrants[final_pos];
    int distance;
    if (final_quad == rook_quad) {
        int manhattan_distance = abs(getx(rook_pos) - getx(final_pos)) + abs(gety(rook_pos) - gety(final_pos));
        U8 rook_coordinate = (rook_quad % 2 == 0 ? getx(rook_pos) : gety(rook_pos));
        U8 final_coordinate = (rook_quad % 2 == 0 ? getx(final_pos) : gety(final_pos));
        if ((((rook_quad == 1 || rook_quad == 2) && rook_coordinate >= final_coordinate) || ((rook_quad == 0 || rook_quad == 3) && rook_coordinate <= final_coordinate)) || manhattan_distance == 1) {
            distance = 1;
        } else {
            distance = 3;
        }
    } else {
        int quad_diff = (final_quad > rook_quad ? final_quad - rook_quad : 4 + final_quad - rook_quad);
        distance = max(1, quad_diff);
    }
    return distance;
}

Evaluation eval(Board& b) {

    U8 white_pieces[6] = {b.data.w_rook_1, b.data.w_rook_2, b.data.w_king, b.data.w_bishop, b.data.w_pawn_1, b.data.w_pawn_2};
    U8 black_pieces[6] = {b.data.b_rook_1, b.data.b_rook_2, b.data.b_king, b.data.b_bishop, b.data.b_pawn_1, b.data.b_pawn_2};

    U8* player_pieces = (curr_player == WHITE ? white_pieces : black_pieces);
    U8* opponent_pieces = (curr_player == WHITE ? black_pieces : white_pieces);

    U8 player_promo = (curr_player == WHITE ? pos(4, 5) : pos(2, 0));
    U8 opponent_promo = (curr_player == WHITE ? pos(2, 0) : pos(4, 5));

    U8 player_king = player_pieces[2];
    U8 opponent_king = opponent_pieces[2];

    unordered_set<U16> player_moves, opponent_moves;
    (curr_player == b.data.player_to_play ? player_moves : opponent_moves) = b.get_legal_moves();
    flip_player(b);
    (curr_player == b.data.player_to_play ? player_moves : opponent_moves) = b.get_legal_moves();
    flip_player(b);

    Evaluation score;

    for (int i = 4; i < 6; i++) {
        if (player_pieces[i] == DEAD) {

        } else if (b.data.board_0[player_pieces[i]] & ROOK) {
            PLAYER_WEIGHTS[i] = ROOK_WEIGHT;
        } else if (b.data.board_0[player_pieces[i]] & BISHOP) {
            PLAYER_WEIGHTS[i] = BISHOP_WEIGHT;
        } else {
            PLAYER_WEIGHTS[i] = PAWN_WEIGHT;
            int piece_y = gety(player_pieces[i]);
            int distance_y = min(abs(piece_y - gety(player_promo)), abs(piece_y - gety(player_promo) - 1));
            int pawn_distance = get_distance(player_pieces[i], player_promo);
            int promo_score;
            if (distance_y <= 1) {
                promo_score = 250 / (1 + pawn_distance);
            } else if (distance_y <= 3){
                promo_score = 180 / (1 + pawn_distance);
            } else {
                promo_score = 150 / (1 + pawn_distance);
            }
            PLAYER_WEIGHTS[i] += promo_score;
        }
        if (opponent_pieces[i] == DEAD) {

        } else if (b.data.board_0[opponent_pieces[i]] & ROOK) {
            OPPONENT_WEIGHTS[i] = ROOK_WEIGHT;
        } else if (b.data.board_0[opponent_pieces[i]] & BISHOP) {
            OPPONENT_WEIGHTS[i] = BISHOP_WEIGHT;
        } else {
            OPPONENT_WEIGHTS[i] = PAWN_WEIGHT;
            int piece_y = gety(opponent_pieces[i]);
            int distance_y = min(abs(piece_y - gety(opponent_promo)), abs(piece_y - gety(opponent_promo) - 1));
            int pawn_distance = get_distance(opponent_pieces[i], opponent_promo);
            int promo_score;
            if (distance_y <= 1) {
                promo_score = 250 / (1 + pawn_distance);
            } else if (distance_y <= 3){
                promo_score = 180 / (1 + pawn_distance);
            } else {
                promo_score = 150 / (1 + pawn_distance);
            }
            OPPONENT_WEIGHTS[i] += promo_score;
        }
    }

    auto add_piece_score = [&]() {
        for (int i = 0; i < 6; i++) {
            if (player_pieces[i] != DEAD) {
                score.piece_weight += PLAYER_WEIGHTS[i];
            }
            if (opponent_pieces[i] != DEAD) {
                score.piece_weight -= OPPONENT_WEIGHTS[i];
            }
        }
    };

    auto add_attack_score = [&]() {
        for (auto move : player_moves) {
            U8 final_pos = getp1(move);
            for (int i = 0; i < 6; i++) {
                if (final_pos == opponent_pieces[i] && i != 2) {
                    score.attack += OPPONENT_WEIGHTS[i] / ATTACKING_FACTOR;
                }
            }
        }
        for (auto move : opponent_moves) {
            U8 final_pos = getp1(move);
            for (int i = 0; i < 6; i++) {
                if (final_pos == player_pieces[i] && i != 2) {
                    score.attack -= PLAYER_WEIGHTS[i] / DEFENDING_FACTOR;
                }
            }
        }
    };

    auto subtract_check_score = [&]() {
        if (b.in_check()) {
            if (b.get_legal_moves().empty()) {
                score.reset();
                score.check = (b.data.player_to_play == curr_player ? INT_MIN : INT_MAX);
            } else {
                score.check += (b.data.player_to_play == curr_player ? -1 : 1) * CHECK_WEIGHT;
            }
        }
    };

    auto calc_promo_score = [&](U8* pieces, U8 promo_pos) {
        int promo_score = 0;
        U8 promo_pos_y = gety(promo_pos);
        U8 piece_y;
        int distance_y, pawn_distance;
        int min_promo = INT_MAX;
        int max_promo = INT_MIN;
        for (int i = 4; i < 6; i++) {
            if (pieces[i] == DEAD || !(b.data.board_0[pieces[i]] & PAWN)) {
                continue;
            }
            piece_y = gety(pieces[i]);
            distance_y = min(abs(piece_y - promo_pos_y), abs(piece_y - promo_pos_y - 1));
            pawn_distance = get_distance(pieces[i], promo_pos);
            if (distance_y <= 1) {
                promo_score = 120 / (1 + pawn_distance);
            } else if (distance_y <= 3){
                promo_score = 100 / (1 + pawn_distance);
            } else {
                promo_score = 50 / (1 + pawn_distance);
            }
            min_promo = min(promo_score, min_promo);
            max_promo = max(promo_score, max_promo);
        }
        if (min_promo != INT_MAX) {
            promo_score = (max_promo + 10 * min_promo) / 11;
        }
        return promo_score;
    };

    auto add_promo_score = [&]() {
        score.promo += calc_promo_score(player_pieces, player_promo);
        score.promo -= calc_promo_score(opponent_pieces, opponent_promo);
    };

    auto add_king_distance_score = [&]() {
        int distance;
        for (int i = 0; i < 6; i++) {
            if (player_pieces[i] == DEAD || i == 2) {
                continue;
            }
            if (i <= 1) {
                distance = get_rook_distance(player_pieces[i], opponent_king);
                score.king_distance += PLAYER_WEIGHTS[i] / (40 + 10 * distance);
            } else {
                distance = get_distance(player_pieces[i], opponent_king);
                score.king_distance += PLAYER_WEIGHTS[i] / (20 + 10 * distance);
            }
        }
        for (int i = 0; i < 6; i++) {
            if (opponent_pieces[i] == DEAD || i == 2) {
                continue;
            }
            if (i <= 1) {
                distance = get_rook_distance(opponent_pieces[i], player_king);
                score.king_distance -= OPPONENT_WEIGHTS[i] / (40 + 10 * distance);
            } else {
                distance = get_distance(opponent_pieces[i], player_king);
                score.king_distance -= OPPONENT_WEIGHTS[i] / (20 + 10 * distance);
            }
        }
    };

    // auto add_ring_score = [&]() {
    //     for (int i = 0; i < 2; i++) {
    //         if (player_pieces[i] == DEAD) {
    //             continue;
    //         }
    //         int piece_x = getx(player_pieces[i]);
    //         int piece_y = gety(player_pieces[i]);
    //         if (min(piece_x, 6 - piece_x) == 0 || min(piece_y, 6 - piece_y) == 0) {
    //             score.ring_weight += RING_WEIGHT;
    //         }
    //     }
    //     for (int i = 0; i < 2; i++) {
    //         if (opponent_pieces[i] == DEAD) {
    //             continue;
    //         }
    //         int piece_x = getx(opponent_pieces[i]);
    //         int piece_y = gety(opponent_pieces[i]);
    //         if (min(piece_x, 6 - piece_x) == 0 || min(piece_y, 6 - piece_y) == 0) {
    //             score.ring_weight -= RING_WEIGHT;
    //         }
    //     }
    // };

    add_piece_score();
    add_attack_score();
    add_promo_score();
    add_king_distance_score();
    subtract_check_score();
    // add_ring_score();

    score.update_total();

    return score;
}

bool is_equal(Board* b1, Board* b2) {
    bool is_king_equal = (b1->data.b_king == b2->data.b_king) && (b1->data.w_king == b2->data.w_king);
    bool is_rook_1_equal = (b1->data.b_rook_1 == b2->data.b_rook_1) && (b1->data.w_rook_1 == b2->data.w_rook_1);
    bool is_rook_2_equal = (b1->data.b_rook_2 == b2->data.b_rook_2) && (b1->data.w_rook_2 == b2->data.w_rook_2);
    bool is_pawn_1_equal = (b1->data.b_pawn_1 == b2->data.b_pawn_1) && (b1->data.w_pawn_1 == b2->data.w_pawn_1);
    bool is_pawn_2_equal = (b1->data.b_pawn_2 == b2->data.b_pawn_2) && (b1->data.w_pawn_2 == b2->data.w_pawn_2);
    bool is_bishop_equal = (b1->data.b_bishop == b2->data.b_bishop) && (b1->data.w_bishop == b2->data.w_bishop);
    bool is_equal = is_king_equal && is_rook_1_equal && is_rook_2_equal && is_pawn_1_equal && is_pawn_2_equal && is_bishop_equal;
    return is_equal;
}

bool is_better_eval(Evaluation& eval1, Evaluation& eval2, bool maximizing_player) {
    bool res = (maximizing_player ? eval1.total > eval2.total : eval1.total < eval2.total);
    res = res || (eval1.total == eval2.total && eval1.depth < eval2.depth);
    return res;
}

Evaluation minimax(Board& board, int depth, bool maximizing_player, vector<Board*> &visited, int alpha, int beta, chrono::milliseconds &time_left) {
    Evaluation best_eval;
    if (previous_board_occurences[board_to_str(&board.data)] == 2) {
        best_eval.total = (maximizing_player ? 1 : -1) * REPETITION_WEIGHT;
        return best_eval;
    }
    if (depth == 0) {
        return eval(board);
    }
    best_eval.total = (maximizing_player ? INT_MIN : INT_MAX);
    auto player_moveset = board.get_legal_moves();
    if (player_moveset.empty() && !board.in_check()) {
        best_eval.total = (maximizing_player ? 1 : -1) * STALEMATE_WEIGHT;
        return best_eval;
    }
    auto start_time = chrono::high_resolution_clock::now();
    auto end_time = start_time + 0.1 * time_left;
    for (auto iter = player_moveset.begin(); iter != player_moveset.end() && (chrono::high_resolution_clock::now() < end_time); iter++) {
        auto move = *iter;
        Board* new_board = new Board(board);
        new_board->do_move_(move);
        bool is_visited_board = false;
        for (Board* b : visited) {
            if (is_equal(b, new_board)) {
                is_visited_board = true;
                break;
            }
        }
        if (is_visited_board) {
            delete new_board;
            continue;
        }
        visited.push_back(new_board);
        nodes_visited++;
        Evaluation eval = minimax(*new_board, depth - 1, !maximizing_player, visited, alpha, beta, time_left);
        eval.depth++;
        eval.moves.push_back(move);
        delete new_board;
        visited.pop_back();
        if (is_better_eval(eval, best_eval, maximizing_player)) {
            best_eval = eval;
        }
        if (maximizing_player) {
            alpha = max(alpha, best_eval.total);
        } else {
            beta = min(beta, best_eval.total);
        }
        if (alpha >= beta) {
            break;
        }
    }
    return best_eval;
}

void Engine::find_best_move(const Board& b) {
    auto start_time = chrono::high_resolution_clock::now();
    previous_board_occurences[board_to_str(&b.data)]++;
    if (curr_player == -1) {
        curr_player = b.data.player_to_play;
        init_quadrant_map();
    }
    Evaluation best_eval;
    best_eval.total = INT_MIN;
    best_eval.depth = MAX_SEARCH_DEPTH;
    auto player_moveset = b.get_legal_moves();
    this->best_move = 0;
    vector<Board*> visited;
    nodes_visited = 0;
    auto end_time = start_time + 0.1 * this->time_left;
    for (int depth = MIN_SEARCH_DEPTH - 1; depth < MAX_SEARCH_DEPTH && (chrono::high_resolution_clock::now() < end_time); depth++) {
        int alpha = INT_MIN;
        int beta = INT_MAX;
        for (auto iter = player_moveset.begin(); iter != player_moveset.end() && (chrono::high_resolution_clock::now() < end_time); iter++) {
            auto move = *iter;
            Board* new_board = new Board(b);
            new_board->do_move_(move);
            visited.push_back(new_board);
            nodes_visited++;
            Evaluation eval = minimax(*new_board, depth, false, visited, alpha, beta, this->time_left);
            eval.depth++;
            visited.pop_back();
            if (is_better_eval(eval, best_eval, true)) {
                for (int i = eval.moves.size() - 1; i >= 0; i--) {
                    new_board->do_move_(eval.moves[i]);
                }
                nodes_visited++;
                Evaluation new_eval = minimax(*new_board, QUIESCENCE_DEPTH, (eval.depth % 2 == 0), visited, INT_MIN, INT_MAX, this->time_left);
                if (new_eval.total - eval.total >= 0 || best_eval.total == INT_MIN) {
                    best_eval = eval;
                    this->best_move = move;
                    alpha = eval.total;
                }
            }
            delete new_board;
        }
    }
    end_time = chrono::high_resolution_clock::now();
    Board* new_board = new Board(b);
    new_board->do_move_(best_move);
    previous_board_occurences[board_to_str(&new_board->data)]++;
    delete new_board;
    best_eval.print();
    cout << "found best move in " << chrono::duration_cast<chrono::duration<double>>(end_time - start_time).count() << " seconds" << endl;
    cout << "nodes visited " << nodes_visited << endl;
}
