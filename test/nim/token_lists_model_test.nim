## Tokens-submodel lifetime tests for token_lists_model. Compile with
## -d:QT_MODEL_SPY. Consumers cache the pointer returned for the "tokens" role,
## so a submodel must stay stable per row and die only with its row.

import unittest, tables
import nimqml

import app/modules/main/wallet_section/all_tokens/token_lists_model
import app/modules/main/wallet_section/all_tokens/tokens_model
import app/modules/main/wallet_section/all_tokens/io_interface
import app_service/service/token/items/token_list
import app_service/service/token/dto/token_list as token_list_dto

var gLists: seq[TokenListItem] = @[]

proc listsDataSource(): TokenListsModelDataSource =
  (
    getAllTokenLists: proc(): var seq[TokenListItem] = gLists,
  )

proc mkList(id: string, tokenCount: int = 1): TokenListItem =
  var toks: seq[TokenDtoSafe] = @[]
  for i in 0 ..< tokenCount:
    toks.add(TokenDto(chainId: 1, address: "0x" & id & $i, crossChainId: id & $i,
      name: id, symbol: id, decimals: 18))
  createTokenListItem(TokenListDto(id: id, name: id, timestamp: "",
    fetchedTimestamp: "", source: "", version: VersionDto(), logoUri: "",
    tokens: toks))

proc tokensRole(m: TokenListsModel): int =
  for k, v in m.roleNames():
    if v == "tokens": return k
  doAssert false, "tokens role not found"

proc readTokensRole(m: TokenListsModel, row: int) =
  ## Read the tokens role through data(), the way the Qt view layer does.
  let idx = m.createIndex(row, 0, nil)
  defer: idx.delete
  let v = m.data(idx, m.tokensRole())
  if not v.isNil: v.delete

suite "token_lists_model - tokens submodel lifetime":

  test "re-reading the tokens role never destroys a previously handed-out submodel":
    gLists = @[mkList("la"), mkList("lb")]
    let m = newTokenListsModel(listsDataSource())
    m.modelsUpdated()

    let before = tokensModelDeleteCount
    m.readTokensRole(0)   # hand out row la's submodel (QML caches the pointer)
    m.readTokensRole(1)   # reading another row must not kill row la's submodel
    m.readTokensRole(0)   # re-reading the same row must not kill row lb's either
    check tokensModelDeleteCount == before

  test "same submodel instance handed out for the same list across reads and refreshes":
    gLists = @[mkList("la"), mkList("lb")]
    let m = newTokenListsModel(listsDataSource())
    m.modelsUpdated()

    let subA = m.ensureTokensSubmodel("la")
    m.readTokensRole(0)                       # data() must reuse, not re-create
    check m.ensureTokensSubmodel("la") == subA

    gLists = @[mkList("la"), mkList("lb")]    # stable refresh
    m.modelsUpdated()
    check m.ensureTokensSubmodel("la") == subA

  test "submodel follows its list id when rows shift, not its creation index":
    gLists = @[mkList("la"), mkList("lb", tokenCount = 2)]
    let m = newTokenListsModel(listsDataSource())
    m.modelsUpdated()

    let subB = m.ensureTokensSubmodel("lb")
    check subB.rowCount(nil) == 2

    gLists = @[mkList("lb", tokenCount = 2), mkList("la")]  # lb moves to row 0
    m.modelsUpdated()
    check subB.rowCount(nil) == 2
    check m.ensureTokensSubmodel("la").rowCount(nil) == 1

  test "removed list's submodel is deleted after the reset; a returning id gets a fresh one":
    gLists = @[mkList("la"), mkList("lb")]
    let m = newTokenListsModel(listsDataSource())
    m.modelsUpdated()

    discard m.ensureTokensSubmodel("la")
    let before = tokensModelDeleteCount

    gLists = @[mkList("lb")]
    m.modelsUpdated()
    # Row gone -> its submodel is dropped and destroyed (consumers were notified
    # by the model reset and detach via destroyed()).
    check tokensModelDeleteCount == before + 1

    gLists = @[mkList("lb"), mkList("la")]
    m.modelsUpdated()
    check m.ensureTokensSubmodel("la").rowCount(nil) == 1   # fresh instance

  test "surviving submodels reset with the parent's full reset":
    # The lists model always resets fully; its live submodels follow suit.
    gLists = @[mkList("la"), mkList("lb")]
    let m = newTokenListsModel(listsDataSource())
    m.modelsUpdated()
    discard m.ensureTokensSubmodel("la")

    let before = tokensModelResetCount
    gLists = @[mkList("la", tokenCount = 2), mkList("lb")]
    m.modelsUpdated()
    check tokensModelResetCount == before + 1
    check m.ensureTokensSubmodel("la").rowCount(nil) == 2
