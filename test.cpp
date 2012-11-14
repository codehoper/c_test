#include<iostream>
#include<vector>
using namespace std;

//pass by value
typedef vector <int> IntVector;
string rev_char(string str);
string rev_string(string str);

enum token_types_t {
    IDENT,  // a sequence of non-space characters not starting with "
    STRING, // sequence of characters between " ", no escape
    ENDTOK, // end of string/file, no more token
    ERRTOK  // unrecognized token
};

struct Token {
    token_types_t type;
    std::string value;
    // constructor for Token
    Token(token_types_t tt=ENDTOK, std::string val="") : type(tt), value(val) {}
};

class Lexer {
    public:
    // constructor
    Lexer(std::string str="") : input_str(str), cur_pos(0), in_err(false),
        separators(" \r\t\n") { }

    void set_input(std::string); // set a new input,
    void restart();              // move cursor to the beginning, restart

    Token next_token();    // returns the next token
    bool has_more_token(); // are there more token(s)?

    private:
    std::string input_str;  // the input string to be scanned
    size_t      cur_pos;    // current position in the input string
    bool        in_err;     // are we in the error state?
    std::string separators; // set of separators; *not* the best option!
};

/**
 * -----------------------------------------------------------------------------
 *  scan and return the next token
 *  cur_pos then points to one position right past the token
 *  the token type is set to ERRTOK on error, at that point the global state
 *  variable err will be set to true
 * -----------------------------------------------------------------------------
 */
Token Lexer::next_token() {
    Token ret;
    size_t last;

    if (in_err) {
        ret.type = ERRTOK;
        ret.value = "";
        return ret;
    }

    // if not in error state, the default token is the ENDTOK
    ret.type = ENDTOK;
    ret.value = "";

    if (has_more_token()) {
        last = cur_pos; // input_str[last] is a non-space char
        if (input_str[cur_pos] == '"') {
            cur_pos++;
            while (cur_pos < input_str.length() && input_str[cur_pos] != '"')
                cur_pos++;
            if (cur_pos < input_str.length()) {
                ret.type = STRING;
                ret.value = input_str.substr(last+1, cur_pos-last-1);
                cur_pos++; // move past the closing "
            } else {
                in_err = true;
                ret.type = ERRTOK;
                ret.value = "";
            }
        } else {
            while (cur_pos < input_str.length() &&
                   separators.find(input_str[cur_pos]) == string::npos &&
                   input_str[cur_pos] != '"') {
                cur_pos++;
            }
            ret.type  = IDENT;
            ret.value = input_str.substr(last, cur_pos-last);
        }
    }
    return ret;
}

/**
 * -----------------------------------------------------------------------------
 *  set a new input string, restart
 * -----------------------------------------------------------------------------
 */
void Lexer::set_input(string str) {
    input_str = str;
    restart();
}

/**
 * -----------------------------------------------------------------------------
 *  is there more token from the current position?
 * -----------------------------------------------------------------------------
 */
bool Lexer::has_more_token() {
    while (cur_pos < input_str.length() &&
           separators.find(input_str[cur_pos]) != string::npos) {
        cur_pos++;
    }
    return (cur_pos < input_str.length());
}

/**
 * -----------------------------------------------------------------------------
 *  restart from the beginning, reset error states
 * -----------------------------------------------------------------------------
 */
void Lexer::restart() {
    cur_pos = 0;
    in_err = false;
}
int main() {
	string hi = "hi";
	string world = "world";
	string bye = "bye";
	bool is_true = true;
	if(is_true) {
		cout << hi << " " << world << endl;
	}else {
		cout << bye << world<<"\n";
	}

	IntVector i;
	int inside_trading(int);
	int inside_trading(int);
	inside_trading(12);
	rev_char("Dev");

	Lexer lex("Hi hello \n test RGGB");
	lex.set_input("Test \n test\n \t");

}

string rev_char(string str) {
	string rev = "";
	for(int i=str.length() -1;i>=0;i--) {
		rev.push_back(str[i]);
	}
	//cout << "reverse string " << rev << endl;
	return rev;
}

int inside_trading(int tax) {
	cout << "Inside tax value " << tax << endl;
	return tax;
}

