#ifndef EXPANDER_HH
#define EXPANDER_HH

#include <vector>

#include "Lexicon.hh"
#include "Hmm.hh"
#include "Acoustics.hh"

class Expander {
public:

  class Word {
  public:
    Word() : 
      avg_log_prob(0), log_prob(0), frames(-1), word_id(-1), active(false) { }
    float avg_log_prob;
    float log_prob;
    int frames;
    int word_id;
    bool active;
  };

  struct WordCompare {
    inline bool operator()(const Word *a, const Word *b) {
      return a->avg_log_prob > b->avg_log_prob;
    }
  };

  // Actions
  Expander(const std::vector<Hmm> &hmms, Lexicon &lexicon,
	   Acoustics &m_acoustics);
  void expand(int start_frame, int frames);

  // Options
  void set_forced_end(bool forced_end) { m_forced_end = forced_end; }
  void set_token_limit(int limit) { m_token_limit = limit; }
  void set_beam(float beam) { m_beam = beam; }
  void set_max_state_duration(int duration) { m_max_state_duration = duration;}
  void sort_words() { std::sort(m_found_words.begin(), m_found_words.end(), 
				Expander::WordCompare()); }

  // Info
  inline std::vector<Lexicon::Token*> &tokens() { return m_tokens; }

  /**
   * Returns the list of the best words.
   *
   * It is ok to modify this vector freely.  The vector will be
   * cleared anyway when expand() is called.
   **/
  inline std::vector<Word*> &words() { return m_found_words; }

  inline Word* word(int index) { return &m_words[index]; }

  // Debug
  void debug_print_history(Lexicon::Token *token);
  void debug_print_timit(Lexicon::Token *token);
  void debug_print_tokens();

private:
  void sort_best_tokens(int tokens);
  void keep_best_tokens(int tokens);
  void move_all_tokens();
  void clear_tokens();
  void create_initial_tokens(int start_frame);
  Lexicon::Token *token_to_state(const Lexicon::Token *source_token,
				 Lexicon::State &source_state,
				 Lexicon::State &target_state,
				 float new_log_prob);

  const std::vector<Hmm> &m_hmms;
  Lexicon &m_lexicon;
  Acoustics &m_acoustics;

  // Options
  bool m_forced_end;
  int m_token_limit;
  float m_beam;
  int m_max_state_duration;

  // State
  std::vector<Lexicon::Token*> m_tokens;
  std::vector<Word> m_words;
  std::vector<Word*> m_found_words;
  int m_frame; // Current frame relative to the start frame.
  int m_frames; // Max frames per word
  float m_beam_best;
  float m_beam_best_tmp;
};

#endif /* EXPANDER_HH */
