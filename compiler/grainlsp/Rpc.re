[@deriving yojson]
type completionValues = {
  resolveProvider: bool,
  triggerCharacters: list(string),
};

[@deriving yojson]
type signatureHelpers = {triggerCharacters: list(string)};

[@deriving yojson]
type codeValues = {resolveProvider: bool};

[@deriving yojson]
type lspCapabilities = {
  documentFormattingProvider: bool,
  textDocumentSync: int,
  hoverProvider: bool,
  completionProvider: completionValues,
  signatureHelpProvider: signatureHelpers,
  definitionProvider: bool,
  typeDefinitionProvider: bool,
  referencesProvider: bool,
  documentSymbolProvider: bool,
  codeActionProvider: bool,
  codeLensProvider: codeValues,
  documentHighlightProvider: bool,
  documentRangeFormattingProvider: bool,
  renameProvider: bool,
};

[@deriving yojson]
type lens_t = {
  line: int,
  signature: string,
};
[@deriving yojson]
type range_t = {
  start_line: int,
  start_char: int,
  end_line: int,
  end_char: int,
};

type protocolMsg =
  | Message(int, string, Yojson.Safe.t)
  | Error(string)
  | Notification(string, Yojson.Safe.t);

let readMessage = (log, input): protocolMsg => {
  let clength = input_line(input);
  // log("read " ++ clength);
  let cl = "Content-Length: ";
  let cll = String.length(cl);
  if (String.sub(clength, 0, cll) == cl) {
    /* if on windows, dont need the extra -1 */
    let offset = Sys.os_type == "Win32" ? 0 : (-1); /* -1 for trailing \r */

    let num =
      String.sub(clength, cll, String.length(clength) - cll + offset);
    // log("Num bytes to read: " ++ String.escaped(num));
    let num = (num |> int_of_string) + (Sys.os_type == "Win32" ? 1 : 2);
    let buffer = Buffer.create(num);
    Buffer.add_channel(buffer, input, num);
    let raw = Buffer.contents(buffer);
    //log("Read message " ++ raw);

    let json = Yojson.Safe.from_string(raw);

    switch (json) {
    | `Assoc(items) => ()
    | _ => ()
    };

    let action =
      Yojson.Safe.Util.member("method", json) |> Yojson.Safe.Util.to_string;

    let idOpt =
      Yojson.Safe.Util.member("id", json) |> Yojson.Safe.Util.to_int_option;

    log("Action is " ++ action);

    switch (idOpt) {
    | None =>
      log("notificiation");
      Notification(action, json);
    | Some(id) =>
      log("message with id " ++ string_of_int(id));
      Message(id, action, json);
    };
  } else {
    failwith("Invalid header");
  };
};

let send = (output, content) => {
  let length = String.length(content);
  let sep = Sys.os_type == "Unix" ? "\r\n\r\n" : "\n\n";

  let len = string_of_int(length);

  output_string(output, "Content-Length: " ++ len ++ sep ++ content);
  flush(output);
};

[@deriving yojson]
type response_error = {
  code: int,
  message: string,
};
[@deriving yojson]
type lsp_error_message = {
  id: int,
  error: response_error,
};

let sendMessage = (log, output, id: int) => {
  let error =
    `Assoc([
      ("code", `Int(-32603)),
      ("message", `String("Not yet implemented")),
    ]);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("error", error),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  log("sending error " ++ strJson);
  send(output, strJson);
  log("sent");
};

let sendCapabilities = (log, output, id: int) => {
  let sigHelpers: signatureHelpers = {triggerCharacters: ["("]};

  let completionVals: completionValues = {
    resolveProvider: true,
    triggerCharacters: ["."],
  };

  let codeVals: codeValues = {resolveProvider: true};

  let capabilities: lspCapabilities = {
    documentFormattingProvider: false,
    textDocumentSync: 1,
    hoverProvider: true,
    completionProvider: completionVals,
    signatureHelpProvider: sigHelpers,
    definitionProvider: true,
    typeDefinitionProvider: false,
    referencesProvider: false,
    documentSymbolProvider: false,
    codeActionProvider: true,
    codeLensProvider: codeVals,
    documentHighlightProvider: false,
    documentRangeFormattingProvider: false,
    renameProvider: false,
  };

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      (
        "result",
        `Assoc([("capabilities", lspCapabilities_to_yojson(capabilities))]),
      ),
    ]);

  let strJson = Yojson.Safe.to_string(res);

  log("sending " ++ strJson);
  send(output, strJson);
  log("sent");
};

let sendNotificationMessage = (log, output, message) => {
  let notification =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("method", `String("window/showMessage")),
      (
        "params",
        `Assoc([("type", `Int(3)), ("message", `String(message))]),
      ),
    ]);

  let jsonMessage = Yojson.Basic.to_string(notification);
  log("sending message notification " ++ jsonMessage);

  send(output, jsonMessage);
};

let sendSignature = (log, output, id: int, sigs: list(string)) => {
  let sigInfos =
    List.map(
      s => {
        let sigInfo =
          `Assoc([
            ("label", `String("someval")),
            ("detail", `String(s)),
            // (
            //   "documentation",
            //   `String("Number addition. Left-associative operator"),
            // ),
          ]);
        sigInfo;
      },
      sigs,
    );

  let sigInfo =
    `Assoc([
      ("isIncomplete", `Bool(false)),
      ("signatures", `List(sigInfos)),
    ]);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("result", sigInfo),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  log("sending signature " ++ strJson);
  send(output, strJson);
  log("sent");
};

let sendDiagnostics =
    (
      log,
      output,
      uri,
      error: option(Grain_diagnostics.Output.lsp_error),
      warnings: option(list(Grain_diagnostics.Output.lsp_warning)),
    ) => {
  let errorDiags =
    switch (error) {
    | None => []
    | Some(err) =>
      let rstart =
        `Assoc([
          ("line", `Int(err.line)),
          ("character", `Int(err.startchar)),
        ]);
      let rend =
        `Assoc([
          ("line", `Int(err.endline)),
          ("character", `Int(err.endchar)),
        ]);

      let range = `Assoc([("start", rstart), ("end", rend)]);

      [
        `Assoc([
          ("range", range),
          ("severity", `Int(1)),
          ("message", `String(err.lsp_message)),
        ]),
      ];
    };

  let with_warnings =
    switch (warnings) {
    | None => errorDiags
    | Some(warns) =>
      let warningDiags =
        List.map(
          (w: Grain_diagnostics.Output.lsp_warning) => {
            let rstart =
              `Assoc([
                ("line", `Int(w.line)),
                ("character", `Int(w.startchar)),
              ]);
            let rend =
              `Assoc([
                ("line", `Int(w.endline)),
                ("character", `Int(w.endchar)),
              ]);

            let range = `Assoc([("start", rstart), ("end", rend)]);

            `Assoc([
              ("range", range),
              ("severity", `Int(2)),
              ("message", `String(w.lsp_message)),
            ]);
          },
          warns,
        );
      List.append(errorDiags, warningDiags);
    };

  let diagnostics = `List(with_warnings);

  let notification =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("method", `String("textDocument/publishDiagnostics")),
      (
        "params",
        `Assoc([("uri", `String(uri)), ("diagnostics", diagnostics)]),
      ),
    ]);

  let jsonMessage = Yojson.Basic.to_string(notification);
  log("sending message notification " ++ jsonMessage);

  send(output, jsonMessage);
};

let clearDiagnostics = (log, output, uri) => {
  let notification =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("method", `String("textDocument/publishDiagnostics")),
      (
        "params",
        `Assoc([("uri", `String(uri)), ("diagnostics", `List([]))]),
      ),
    ]);

  let jsonMessage = Yojson.Basic.to_string(notification);
  log("sending message notification " ++ jsonMessage);

  send(output, jsonMessage);
};

let sendLenses = (log, output, id: int, lenses: list(lens_t)) => {
  let convertedLenses =
    List.map(
      l => {
        let rstart =
          `Assoc([("line", `Int(l.line - 1)), ("character", `Int(1))]);
        let rend =
          `Assoc([("line", `Int(l.line - 1)), ("character", `Int(1))]);

        let range = `Assoc([("start", rstart), ("end", rend)]);

        `Assoc([("range", range), ("data", `String(l.signature))]);
      },
      lenses,
    );

  let lensesJson = `List(convertedLenses);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("result", lensesJson),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  //log("sending message " ++ strJson);
  send(output, strJson);
  log("sent");
};

let sendRefreshLenses = (log, output) => {
  let notification =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("method", `String("grainlsp/lensesLoaded")),
    ]);

  let jsonMessage = Yojson.Basic.to_string(notification);
  // log("sending message notification " ++ jsonMessage);

  send(output, jsonMessage);
};

let sendHover = (log, output, id: int, signature, range: range_t) => {
  let rstart =
    `Assoc([
      ("line", `Int(range.start_line - 1)),
      ("character", `Int(range.start_char)),
    ]);
  let rend =
    `Assoc([
      ("line", `Int(range.end_line - 1)),
      ("character", `Int(range.end_char)),
    ]);

  let range = `Assoc([("start", rstart), ("end", rend)]);

  let hoverInfo =
    `Assoc([("contents", `String(signature)), ("range", range)]);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("result", hoverInfo),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  // log("sending message " ++ strJson);
  send(output, strJson);
  log("sent");
};

let sendGoToDefinition = (log, output, id: int, uri: string, range: range_t) => {
  let rstart =
    `Assoc([
      ("line", `Int(range.start_line - 1)),
      ("character", `Int(range.start_char)),
    ]);
  let rend =
    `Assoc([
      ("line", `Int(range.end_line - 1)),
      ("character", `Int(range.end_char)),
    ]);

  let range = `Assoc([("start", rstart), ("end", rend)]);
  let location = `Assoc([("uri", `String(uri)), ("range", range)]);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("result", location),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  log("sending message " ++ strJson);
  send(output, strJson);
  log("sent");
};

type completionItem = {
  label: string,
  kind: int,
  detail: string,
};

let sendCompletion =
    (log, output, id: int, completions: list(completionItem)) => {
  let items =
    List.map(
      item => {
        let sigInfo =
          `Assoc([
            ("label", `String(item.label)),
            ("detail", `String(item.detail)),
            ("kind", `Int(item.kind)),
          ]);
        sigInfo;
      },
      completions,
    );

  let sigInfo =
    `Assoc([("isIncomplete", `Bool(false)), ("items", `List(items))]);

  let res =
    `Assoc([
      ("jsonrpc", `String("2.0")),
      ("id", `Int(id)),
      ("result", sigInfo),
    ]);

  let strJson = Yojson.Basic.pretty_to_string(res);

  log("sending signature " ++ strJson);
  send(output, strJson);
  log("sent");
};