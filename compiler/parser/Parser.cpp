#include "Parser.h"

#include <cassert>

#include "TokenStream.h"
#include "../util.h"

#include "../ast/Block.h"
#include "../ast/Expression.h"
#include "../ast/UnaryExpression.h"
#include "../ast/VariableExpression.h"
#include "../ast/ControlExpression.h"
#include "../ast/CallExpression.h"
#include "../ast/CastExpression.h"
#include "../ast/NoteExpression.h"
#include "../ast/NumberExpression.h"
#include "../ast/PostfixExpression.h"
#include "../ast/MathExpression.h"
#include "../ast/AssignExpression.h"
#include "../ast/Form.h"

using namespace MaximParser;
using namespace MaximAst;

static std::string toUpperCase(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

Parser::Parser(std::unique_ptr<TokenStream> stream) : _stream(std::move(stream)) {

}

std::unique_ptr<Block> Parser::parse() {
    auto block = std::make_unique<Block>();

    while (stream()->peek().type != Token::Type::END_OF_FILE) {
        auto expr = parseExpression();
        block->expressions.push_back(std::move(expr));

        expect(stream()->next(), Token::Type::END_OF_LINE);
    }
}

std::unique_ptr<MaximAst::Expression> Parser::parseExpression() {
    return parseExpression(Precedence::ALL);
}

std::unique_ptr<MaximAst::Expression> Parser::parseExpression(Precedence precedence) {
    auto result = parsePrefix(precedence);
    while (stream()->peek().type != Token::Type::END_OF_LINE) {
        result = parsePostfix(std::move(result), precedence);
    }
    return result;
}

std::unique_ptr<MaximAst::Expression> Parser::parsePrefix(Precedence precedence) {
    auto firstToken = stream()->peek();

    // parse basic expressions
    switch (firstToken.type) {
        // Comment
        case Token::Type::HASH:break;
        case Token::Type::COMMENT_OPEN:break;

            // ControlExpression
        case Token::Type::COLON: return parseColonTokenExpression();

            // Form
        case Token::Type::OPEN_SQUARE: return parseOpenSquareTokenExpression();

            // NoteExpression
        case Token::Type::NOTE: return parseNoteTokenExpression();

            // NumberExpression
        case Token::Type::NUMBER: return parseNumberTokenExpression();

            // StringExpression
        case Token::Type::DOUBLE_STRING: return parseStringTokenExpression();

            // UnaryExpression
        case Token::Type::PLUS:
        case Token::Type::MINUS:
        case Token::Type::NOT:
        case Token::Type::INCREMENT:
        case Token::Type::DECREMENT:
            return parseUnaryTokenExpression();

            // VariableExpression
        case Token::Type::IDENTIFIER: return parseIdentifierTokenExpression();

            // Sub-expression
        case Token::Type::OPEN_BRACKET: return parseSubTokenExpression();

        default:
            throw fail(firstToken);
    }
}

std::unique_ptr<MaximAst::Expression> Parser::parsePostfix(std::unique_ptr<MaximAst::Expression> prefix,
                                                           Precedence precedence) {
    auto nextToken = stream()->peek();
    auto nextPrecedence = operatorToPrecedence(nextToken.type);
    if (precedence > nextPrecedence) return prefix;

    switch (nextToken.type) {
        case Token::Type::CAST:
            return parseCastExpression(std::move(prefix));

        case Token::Type::INCREMENT:
        case Token::Type::DECREMENT:
            return parsePostfixExpression(std::move(prefix));

        case Token::Type::BITWISE_AND:
        case Token::Type::BITWISE_OR:
        case Token::Type::BITWISE_XOR:
        case Token::Type::LOGICAL_AND:
        case Token::Type::LOGICAL_OR:
        case Token::Type::EQUAL_TO:
        case Token::Type::NOT_EQUAL_TO:
        case Token::Type::LT:
        case Token::Type::GT:
        case Token::Type::LTE:
        case Token::Type::GTE:
        case Token::Type::PLUS:
        case Token::Type::MINUS:
        case Token::Type::TIMES:
        case Token::Type::DIVIDE:
        case Token::Type::MODULO:
        case Token::Type::POWER:
            return parseMathExpression(std::move(prefix));

        case Token::Type::ASSIGN:
        case Token::Type::PLUS_ASSIGN:
        case Token::Type::MINUS_ASSIGN:
        case Token::Type::TIMES_ASSIGN:
        case Token::Type::DIVIDE_ASSIGN:
        case Token::Type::MODULO_ASSIGN:
        case Token::Type::POWER_ASSIGN:
            return parseAssignExpression(std::move(prefix));

        default:
            return prefix;
    }
}

std::unique_ptr<MaximAst::Expression> Parser::parseColonTokenExpression() {
    auto colon = stream()->peek();
    return parseControlExpression("", colon.startPos);
}

std::unique_ptr<MaximAst::Expression> Parser::parseOpenSquareTokenExpression() {
    auto form = parseForm();
    auto expr = parseExpression(Precedence::UNARY);
    auto formStart = form->startPos;
    auto exprEnd = expr->endPos;
    return std::make_unique<CastExpression>(std::move(form), std::move(expr), true, formStart, exprEnd);
}

std::unique_ptr<MaximAst::Form> Parser::parseForm() {
    expect(stream()->next(), Token::Type::OPEN_SQUARE);
    auto nameToken = stream()->next();
    expect(nameToken, Token::Type::IDENTIFIER);

    auto form = std::make_unique<Form>(nameToken.content, nameToken.startPos, SourcePos(0, 0));
    parseArguments(form->arguments);
    auto closeToken = stream()->next();
    expect(closeToken, Token::Type::CLOSE_SQUARE);
    form->endPos = closeToken.endPos;

    return form;
}

static std::regex noteRegex("([a-gA-G]#?)([0-9]+)", std::regex::ECMAScript | std::regex::optimize);
static std::array<std::string, 12> noteNames = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

std::unique_ptr<MaximAst::Expression> Parser::parseNoteTokenExpression() {
    auto noteToken = stream()->next();
    expect(noteToken, Token::Type::NOTE);

    std::smatch match;
    assert(std::regex_match(noteToken.content, match, noteRegex));

    auto noteName = toUpperCase(match[1].str());
    auto noteNum = std::distance(noteNames.begin(), std::find(noteNames.begin(), noteNames.end(), noteName));
    if (noteNum >= noteNames.size()) {
        throw ParseError(
                "Ey my man, don't you know that " + noteName + " isn't a valid note?",
                noteToken.startPos, noteToken.endPos
        );
    }

    auto octave = std::stoi(match[2].str());
    auto midiNumber = noteNum + octave * noteNames.size();
    return std::make_unique<NoteExpression>(midiNumber, noteToken.startPos, noteToken.endPos);
}

std::unique_ptr<MaximAst::Expression> Parser::parseNumberTokenExpression() {
    auto numberToken = stream()->next();
    expect(numberToken, Token::Type::NUMBER);

    auto numValue = std::stof(numberToken.content);

    auto endPos = numberToken.endPos;
    auto postMulToken = stream()->peek();
    Form valueForm("lin", postMulToken.startPos, postMulToken.endPos);
    if (postMulToken.type == Token::Type::IDENTIFIER) {
        auto postMulText = toUpperCase(postMulToken.content);

        auto didMatchMul = true;
        if (postMulText[0] == 'K') numValue *= 1e3;
        else if (postMulText[0] == 'M') numValue *= 1e6;
        else if (postMulText[0] == 'G') numValue *= 1e9;
        else if (postMulText[0] == 'T') numValue *= 1e12;
        else if (postMulText[0] == 'P') numValue *= 1e15;
        else didMatchMul = false;

        auto didMatchForm = true;
        auto formStart = didMatchMul ? 1u : 0u;
        if (postMulText.compare(formStart, postMulText.npos, "HZ")) valueForm.name = "freq";
        else if (postMulText.compare(formStart, postMulText.npos, "DB")) valueForm.name = "db";
        else if (postMulText.compare(formStart, postMulText.npos, "Q")) valueForm.name = "q";
        else if (postMulText.compare(formStart, postMulText.npos, "R")) valueForm.name = "res";
        else if (postMulText.compare(formStart, postMulText.npos, "S")) valueForm.name = "seconds";
        else if (postMulText.compare(formStart, postMulText.npos, "B")) valueForm.name = "beats";
        else didMatchForm = false;

        if (didMatchMul || didMatchForm) {
            endPos = postMulToken.endPos;
            stream()->next();
        }
    }

    return std::make_unique<NumberExpression>(numValue, valueForm, numberToken.startPos, endPos);
}

std::unique_ptr<MaximAst::Expression> Parser::parseStringTokenExpression() {
    auto nameToken = stream()->next();
    expect(nameToken, Token::Type::DOUBLE_STRING);
    return parseControlExpression(nameToken.content, nameToken.startPos);
}

std::unique_ptr<MaximAst::Expression> Parser::parseUnaryTokenExpression() {
    auto typeToken = stream()->next();
    UnaryExpression::Type unaryType;
    switch (typeToken.type) {
        case Token::Type::PLUS: unaryType = UnaryExpression::Type::POSITIVE; break;
        case Token::Type::MINUS: unaryType = UnaryExpression::Type::NEGATIVE; break;
        case Token::Type::NOT: unaryType = UnaryExpression::Type::NOT; break;
        default:
            throw fail(typeToken);
    }

    auto expr = parseExpression(Precedence::UNARY);
    auto exprEnd = expr->endPos;
    return std::make_unique<UnaryExpression>(unaryType, std::move(expr), typeToken.startPos, exprEnd);
}

std::unique_ptr<MaximAst::Expression> Parser::parseIdentifierTokenExpression() {
    auto identifier = stream()->next();
    expect(identifier, Token::Type::IDENTIFIER);

    auto nextToken = stream()->peek();
    if (nextToken.type == Token::Type::COLON) {
        parseCallExpression(identifier.content, identifier.startPos);
    } else if (nextToken.type == Token::Type::OPEN_BRACKET) {
        parseControlExpression(identifier.content, identifier.startPos);
    } else {
        return std::make_unique<VariableExpression>(identifier.content, identifier.startPos, identifier.endPos);
    }
}

std::unique_ptr<MaximAst::Expression> Parser::parseCallExpression(std::string name, SourcePos startPos) {
    auto callExpr = std::make_unique<CallExpression>(name, startPos, SourcePos(0, 0));

    expect(stream()->next(), Token::Type::OPEN_BRACKET);
    if (stream()->peek().type != Token::Type::CLOSE_BRACKET) {
        parseArguments(callExpr->arguments);
    }
    auto closeBracket = stream()->next();
    expect(closeBracket, Token::Type::CLOSE_BRACKET);
    callExpr->endPos = closeBracket.endPos;

    return std::move(callExpr);
}

std::unique_ptr<MaximAst::Expression> Parser::parseSubTokenExpression() {
    auto openBracket = stream()->next();
    expect(openBracket, Token::Type::OPEN_BRACKET);
    auto subExpr = parseExpression();
    auto closeBracket = stream()->next();
    expect(closeBracket, Token::Type::CLOSE_BRACKET);
    subExpr->startPos = openBracket.startPos;
    subExpr->endPos = closeBracket.endPos;
    return subExpr;
}

std::unique_ptr<MaximAst::Expression> Parser::parseControlExpression(std::string name, SourcePos startPos) {
    expect(stream()->next(), Token::Type::COLON);
    auto typeToken = stream()->next();
    expect(typeToken, Token::Type::IDENTIFIER);
    ControlExpression::Type controlType;
    if (typeToken.content == "label") controlType = ControlExpression::Type::LABEL;
    else if (typeToken.content == "value") controlType = ControlExpression::Type::VALUE;
    else if (typeToken.content == "toggle") controlType = ControlExpression::Type::TOGGLE;
    else if (typeToken.content == "graph") controlType = ControlExpression::Type::GRAPH;
    else if (typeToken.content == "scope") controlType = ControlExpression::Type::SCOPE;
    else if (typeToken.content == "keys") controlType = ControlExpression::Type::KEYS;
    else if (typeToken.content == "roll") controlType = ControlExpression::Type::ROLL;
    else if (typeToken.content == "plug") controlType = ControlExpression::Type::PLUG;
    else {
        throw ParseError(
                "Come on man, I don't support " + typeToken.content + " controls.",
                typeToken.startPos, typeToken.endPos
        );
    }

    std::string propertyName = "value";

    auto endPos = typeToken.endPos;
    auto propToken = stream()->peek();
    if (propToken.type == Token::Type::DOT) {
        stream()->next();
        auto propertyToken = stream()->next();
        expect(propertyToken, Token::Type::IDENTIFIER);
        propertyName = propertyToken.content;
        endPos = propertyToken.endPos;
    }

    return std::make_unique<ControlExpression>(name, controlType, propertyName, startPos, endPos);
}

void Parser::parseArguments(std::vector<std::unique_ptr<MaximAst::Expression>> &arguments) {
    do {
        arguments.push_back(std::move(parseExpression()));
    } while (stream()->next().type == Token::Type::COMMA);
}

std::unique_ptr<MaximAst::Expression> Parser::parseCastExpression(std::unique_ptr<MaximAst::Expression> prefix) {
    expect(stream()->next(), Token::Type::CAST);
    auto form = parseForm();
    auto formEnd = form->endPos;
    return std::make_unique<CastExpression>(std::move(form), std::move(prefix), false, prefix->startPos, formEnd);
}

std::unique_ptr<MaximAst::Expression> Parser::parsePostfixExpression(std::unique_ptr<MaximAst::Expression> prefix) {
    auto postfixToken = stream()->next();
    PostfixExpression::Type postfixType;
    switch (postfixToken.type) {
        case Token::Type::INCREMENT:
            postfixType = PostfixExpression::Type::INCREMENT;
            break;
        case Token::Type::DECREMENT:
            postfixType = PostfixExpression::Type::DECREMENT;
            break;
        default: throw fail(postfixToken);
    }

    auto assignable = MaximUtil::dynamic_unique_cast<AssignableExpression>(std::move(prefix));
    if (!assignable) throw castFail(assignable.get());

    auto assignableStart = assignable->startPos;
    return std::make_unique<PostfixExpression>(std::move(assignable), postfixType, assignableStart, postfixToken.endPos);
}

std::unique_ptr<MaximAst::Expression> Parser::parseMathExpression(std::unique_ptr<MaximAst::Expression> prefix) {
    auto opToken = stream()->next();
    MathExpression::Type opType;
    switch (opToken.type) {
        case Token::Type::BITWISE_AND:
            opType = MathExpression::Type::BITWISE_AND;
            break;
        case Token::Type::BITWISE_OR:
            opType = MathExpression::Type::BITWISE_OR;
            break;
        case Token::Type::BITWISE_XOR:
            opType = MathExpression::Type::BITWISE_XOR;
            break;
        case Token::Type::LOGICAL_AND:
            opType = MathExpression::Type::LOGICAL_AND;
            break;
        case Token::Type::LOGICAL_OR:
            opType = MathExpression::Type::LOGICAL_OR;
            break;
        case Token::Type::EQUAL_TO:
            opType = MathExpression::Type::LOGICAL_EQUAL;
            break;
        case Token::Type::NOT_EQUAL_TO:
            opType = MathExpression::Type::LOGICAL_NOT_EQUAL;
            break;
        case Token::Type::LT:
            opType = MathExpression::Type::LOGICAL_LT;
            break;
        case Token::Type::GT:
            opType = MathExpression::Type::LOGICAL_GT;
            break;
        case Token::Type::LTE:
            opType = MathExpression::Type::LOGICAL_LTE;
            break;
        case Token::Type::GTE:
            opType = MathExpression::Type::LOGICAL_GTE;
            break;
        case Token::Type::PLUS:
            opType = MathExpression::Type::ADD;
            break;
        case Token::Type::MINUS:
            opType = MathExpression::Type::SUBTRACT;
            break;
        case Token::Type::TIMES:
            opType = MathExpression::Type::MULTIPLY;
            break;
        case Token::Type::DIVIDE:
            opType = MathExpression::Type::DIVIDE;
            break;
        case Token::Type::MODULO:
            opType = MathExpression::Type::MODULO;
            break;
        case Token::Type::POWER:
            opType = MathExpression::Type::POWER;
            break;
        default:
            fail(opToken);
    }
    auto postfix = parseExpression(operatorToPrecedence(opToken.type));
    auto prefixStart = prefix->startPos;
    auto postfixEnd = postfix->endPos;
    return std::make_unique<MathExpression>(std::move(prefix), opType, std::move(postfix), prefixStart, postfixEnd);
}

std::unique_ptr<MaximAst::Expression> Parser::parseAssignExpression(std::unique_ptr<MaximAst::Expression> prefix) {
    auto opToken = stream()->next();
    AssignExpression::Type opType;
    switch (opToken.type) {
        case Token::Type::ASSIGN:
            opType = AssignExpression::Type::ASSIGN;
            break;
        case Token::Type::PLUS_ASSIGN:
            opType = AssignExpression::Type::ADD;
            break;
        case Token::Type::MINUS_ASSIGN:
            opType = AssignExpression::Type::SUBTRACT;
            break;
        case Token::Type::TIMES_ASSIGN:
            opType = AssignExpression::Type::MULTIPLY;
            break;
        case Token::Type::DIVIDE_ASSIGN:
            opType = AssignExpression::Type::DIVIDE;
            break;
        case Token::Type::MODULO_ASSIGN:
            opType = AssignExpression::Type::MODULO;
            break;
        case Token::Type::POWER_ASSIGN:
            opType = AssignExpression::Type::POWER;
            break;
        default:
            fail(opToken);
    }

    auto assignable = MaximUtil::dynamic_unique_cast<AssignableExpression>(std::move(prefix));
    if (!assignable) throw castFail(assignable.get());

    auto postfix = parseExpression(operatorToPrecedence(opToken.type));

    auto assignableStart = assignable->startPos;
    auto postfixEnd = postfix->endPos;
    return std::make_unique<AssignExpression>(std::move(assignable), opType, std::move(postfix), assignableStart, postfixEnd);
}

Parser::Precedence Parser::operatorToPrecedence(Token::Type type) {
    switch (type) {
        case Token::Type::CAST:
            return Precedence::CASTING;
        case Token::Type::INCREMENT:
        case Token::Type::DECREMENT:
            return Precedence::UNARY;
        case Token::Type::BITWISE_AND:
        case Token::Type::BITWISE_OR:
        case Token::Type::BITWISE_XOR:
            return Precedence::BITWISE;
        case Token::Type::PLUS:
            return Precedence::ADD;
        case Token::Type::MINUS:
            return Precedence::SUBTRACT;
        case Token::Type::TIMES:
            return Precedence::MULTIPLY;
        case Token::Type::DIVIDE:
            return Precedence::DIVIDE;
        case Token::Type::MODULO:
            return Precedence::MODULO;
        case Token::Type::POWER:
            return Precedence::POWER;
        case Token::Type::EQUAL_TO:
        case Token::Type::NOT_EQUAL_TO:
        case Token::Type::LT:
        case Token::Type::GT:
        case Token::Type::LTE:
        case Token::Type::GTE:
            return Precedence::EQUALITY;
        case Token::Type::LOGICAL_AND:
        case Token::Type::LOGICAL_OR:
            return Precedence::LOGICAL;
        case Token::Type::ASSIGN:
        case Token::Type::PLUS_ASSIGN:
        case Token::Type::MINUS_ASSIGN:
        case Token::Type::TIMES_ASSIGN:
        case Token::Type::DIVIDE_ASSIGN:
        case Token::Type::MODULO_ASSIGN:
        case Token::Type::POWER_ASSIGN:
            return Precedence::ASSIGNMENT;
        case Token::Type::END_OF_LINE:
        case Token::Type::END_OF_FILE:
        default:
            return Precedence::NONE;
    }
}

void Parser::expect(const Token &token, Token::Type expectedType) {
    if (token.type != expectedType) {
        throw ParseError(
                "Dude, why is there a " + Token::typeString(token.type) + "? I expected a " + Token::typeString(expectedType) + " here.",
                token.startPos, token.endPos
        );
    }
}

ParseError Parser::fail(const Token &token) {
    return ParseError(
            "Hey man, not cool. I didn't expect this " + Token::typeString(token.type) + "!",
            token.startPos, token.endPos
    );
}

ParseError Parser::castFail(MaximAst::Expression *expr) {
    return ParseError(
            "Hey! I ned something I can assign to here, not this silly fuge you're giving me.",
            expr->startPos, expr->endPos
    );
}