#include <fstream>
#include <string>

#include <errno.h>

#include "ArpaNgramReader.hh"

// FIXME:
// - check order when reading

inline void
ArpaNgramReader::regcomp(regex_t *preg, const char *regex, int cflags)
{
  int result = ::regcomp(preg, regex, cflags);
  if (result != 0) {
    char errbuf[4096];
    regerror(result, preg, errbuf, 4096);
    std::cerr << errbuf << std::endl;
    throw RegExpError();
  }
}


inline
bool
ArpaNgramReader::regexec(const regex_t *preg, const char *string)
{
  int result = ::regexec(preg, string, m_matches.size(), &m_matches[0], 0);
  return result == 0;
}

inline
void
ArpaNgramReader::split(std::string &str, std::vector<int> &points) 
{
  bool spaces = true;

  points.clear();
  for (int i = 0; i < m_str.length(); i++) {
    if (spaces) {
      if (str[i] != ' ' && str[i] != '\t') {
	spaces = false;
	points.push_back(i);
      }
    }
    
    else {
      if (str[i] == ' ' || str[i] == '\t') {
	str[i] = '\0';
	spaces = true;
      }
    }
  }
}

ArpaNgramReader::ArpaNgramReader(const Vocabulary &vocabulary)
  : m_vocabulary(vocabulary),
    m_matches(4),
    m_in(NULL)
{
  int cflags = REG_EXTENDED;

  regcomp(&m_r_count, "^ngram ([0-9]+)=([0-9]+)$", cflags);
  regcomp(&m_r_order, "^\\\\([0-9]+)-grams:$", cflags);
}

ArpaNgramReader::~ArpaNgramReader()
{
  regfree(&m_r_count);
  regfree(&m_r_order);
}

inline double 
ArpaNgramReader::str2double(const char *str)
{
  char *endptr;
  double value = strtod(str, &endptr);
  if (endptr == str)
    throw InvalidFloat();
  if (errno == ERANGE)
    throw RangeError();
  return value;
}

inline void
ArpaNgramReader::reset_stacks(int first)
{
  std::fill(&m_word_stack[first], &m_word_stack[m_word_stack.size()], -1);
  std::fill(&m_index_stack[first], &m_index_stack[m_index_stack.size()], -1);
}

inline void
ArpaNgramReader::read_header()
{
  // Skip header
  while (std::getline(*m_in, m_str)) {
    m_lineno++;
    if (m_str == "\\data\\")
      break;
  }
  if (!*m_in)
    throw ReadError();
}

inline void
ArpaNgramReader::read_counts()
{
  int total_ngrams = 0;

  m_counts.clear();
  while (std::getline(*m_in, m_str)) {
    m_lineno++;
    if (!regexec(&m_r_count, m_str.c_str()))
      break;
    m_ngram.m_order = atoi(&m_str.c_str()[start(1)]);
    int value = atoi(&m_str.c_str()[start(2)]);
    total_ngrams += value;
    m_counts.push_back(value);
    if (m_counts.size() != m_ngram.m_order)
      throw InvalidOrder();
  }
  if (!*m_in)
    throw ReadError();

  m_ngram.m_nodes.clear();
  m_ngram.m_nodes.reserve(total_ngrams);
  m_words.reserve(m_ngram.m_order);
  m_word_stack.resize(m_ngram.m_order);
  m_index_stack.resize(m_ngram.m_order - 1); // The last index is not used
  m_points.reserve(m_ngram.m_order + 2); // Words + log_prob and back_off
}

inline void
ArpaNgramReader::read_ngram(int order)
{
  // Divide line in words
  split(m_str, m_points);
  if (m_points.size() < order + 1 || m_points.size() > order + 2)
    throw InvalidNgram();

  // Parse log_prob and back_off
  double log_prob = atof(&m_str[m_points[0]]);
  double back_off = 0;
  if (m_points.size() == order + 2)
    back_off = atof(&m_str[m_points[order + 1]]);

  // Parse words using vocabulary
  m_words.clear();
  for (int i = 0; i < order; i++) {
    int word_id = m_vocabulary.index(&m_str[m_points[i + 1]]);
    m_words.push_back(word_id);
  }

  // Read unigram.  The order of words must follow the order in
  // the vocabulary.
  if (order == 1) {
    if (m_words[0] != m_ngram.m_nodes.size())
      throw UnigramOrder();
    m_ngram.m_nodes.push_back(Ngram::Node(m_words[0], log_prob, back_off));
  }

  // Find the path to the current n-gram
  else {

    // Handle the first word specially
    if (m_word_stack[0] != m_words[0]) {
      m_word_stack[0] = m_words[0];
      m_index_stack[0] = m_words[0];
      reset_stacks(1);
    }

    // Handle the words 2..order-1
    for (int i = 1; i < order-1; i++) {
      int word = m_words[i];

      // Path differs
      if (m_word_stack[i] != word) {
	reset_stacks(i+1);

	Ngram::Node *root = &m_ngram.m_nodes[m_index_stack[i - 1]];
	Ngram::Node *root_next = root + 1;
	int index;

	// Find the correct index
	if (m_index_stack[i] < 0)
	  index = root->first;
	else
	  index = m_index_stack[i] + 1; 
	while (m_ngram.m_nodes[index].word != word) {
	  if (index == root_next->first)
	    throw UnknownPrefix();
	  m_ngram.m_nodes[index].first = m_ngram.m_nodes.size();
	  index++;
	}

	m_word_stack[i] = word;
	m_index_stack[i] = index;
      }
    }

    // Handle the last word, we must not have duplicate
    int word = m_words.back();
    if (m_word_stack[order - 1] == word)
      throw Duplicate();
    m_word_stack[order - 1] = word;

    // Insert the ngram and update root node
    Ngram::Node *root = &m_ngram.m_nodes[m_index_stack[order - 2]];
    if (root->first < 0)
      root->first = m_ngram.m_nodes.size();
    m_ngram.m_nodes.push_back(Ngram::Node(m_words.back(), log_prob, back_off));
  }
}

inline void
ArpaNgramReader::read_ngrams(int order)
{
  bool header = false;

  reset_stacks();

  for (int ngrams_read = 0; ngrams_read < m_counts[order - 1];) {
    if (!std::getline(*m_in, m_str))
      throw ReadError();
    m_lineno++;

    // Skip empty lines
    if (m_str.length() == 0)
      continue;

    // Command
    if (m_str[0] == '\\') {
      if (!header && regexec(&m_r_order, m_str.c_str())) {
	int new_order = atoi(&m_str.c_str()[start(1)]);
	if (new_order != order)
	  throw InvalidOrder();
	header = true;
      }

      else
	throw InvalidCommand();
    }

    // Ngram
    else {
      read_ngram(order);
      ngrams_read++;
    }
  }

  // Fix the 'first' fields.
  if (order > 1) {
    for (int i = m_index_stack[order - 2] + 1; 
	 i < m_ngram.m_nodes.size() - m_counts[order - 1];
	 i++)
      m_ngram.node(i)->first = m_ngram.m_nodes.size();
  }
}

// FIXME: ugly code
void
ArpaNgramReader::read(std::istream &in)
{
  m_in = &in;
  m_str.reserve(512); // Just for efficiency
  m_lineno = 0;

  read_header();
  read_counts();

  // Read all ngrams
  for (int order = 1; order <= m_ngram.m_order; order++)
    read_ngrams(order);

  if (!in)
    throw ReadError();
}

void
ArpaNgramReader::read(const char *file)
{
  std::ifstream in(file);
  if (!in)
    throw OpenError();
  read(in);
}

