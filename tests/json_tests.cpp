#include "framework.hpp"

#include "util/json.hpp"

TEST_CASE(json_parses_nested_values) {
    const auto v = json::parse(R"({"name":"Bot","n":-1.5e2,"ok":true,"none":null,"list":[1,2,{"x":"y"}],
                                   "text":"Zeile\n\"Zitat\" ä😀"})");
    CHECK(v.has_value());
    CHECK_EQ(v->getString("name", ""), std::string("Bot"));
    CHECK_EQ(v->getNumber("n", 0), -150.0);
    CHECK(v->getBool("ok", false));
    CHECK(v->find("none")->isNull());
    CHECK_EQ(v->find("list")->asArray().size(), size_t{3});
    CHECK_EQ(v->find("list")->asArray()[2].getString("x", ""), std::string("y"));
    CHECK_EQ(v->getString("text", ""), std::string("Zeile\n\"Zitat\" \xC3\xA4\xF0\x9F\x98\x80"));
    CHECK_EQ(v->getNumber("fehlt", 7), 7.0);
}

TEST_CASE(json_reports_errors_with_location) {
    std::string err;
    CHECK(!json::parse("{\"a\": 1,\n \"b\" 2}", &err).has_value());
    CHECK(err.find("Zeile 2") != std::string::npos);
    CHECK(!json::parse("[1, 2", &err).has_value());
    CHECK(!json::parse("{\"a\": tru}", &err).has_value());
    CHECK(!json::parse("{} extra", &err).has_value());
    CHECK(json::parse("\xEF\xBB\xBF{\"bom\": 1}").has_value());
}
