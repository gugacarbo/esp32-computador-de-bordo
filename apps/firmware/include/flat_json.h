// flat_json.h — parse de objeto JSON plano só com strings e escape de strings JSON. Usado pelos
// corpos de /api/wifi e /api/obd/config. Portátil: sem Arduino, testado no env native.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace onix {

// {"chave":"valor",...}: só valores string (escapes \" \\ \/ \b \f \n \r \t \uXXXX aceitos),
// espaços em volta permitidos, nada depois do objeto. Pares na ordem do corpo (chave repetida
// aparece de novo; quem lê fica com a última). nullopt = JSON inválido.
using FlatJsonObject = std::vector<std::pair<std::string, std::string>>;
std::optional<FlatJsonObject> parseFlatJsonObject(const std::string& body);

// Acrescenta `text` escapando aspas, barra invertida e controles < 0x20 como \u00XX.
void appendJsonEscaped(std::string& out, const std::string& text);

}  // namespace onix
