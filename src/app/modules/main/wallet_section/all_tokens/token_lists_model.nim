import app/modules/shared_models/model_utils
import nimqml, tables

import io_interface
import tokens_model
type
  ModelRole {.pure.} = enum
    Id = UserRole + 1
    Name
    Timestamp
    FetchedTimestamp
    Source
    Version
    LogoURI
    Tokens

QtObject:
  type TokenListsModel* = ref object of QAbstractListModel
    delegate: io_interface.TokenListsModelDataSource
    # Per-list "tokens" submodels, row lifetime: deleted after the reset that
    # removes their row, so consumers detach via the notification first.
    tokensModels: Table[string, TokensModel]
    emptyTokens: seq[TokenItem]

  proc setup(self: TokenListsModel)
  proc delete(self: TokenListsModel)
  proc newTokenListsModel*(delegate: io_interface.TokenListsModelDataSource): TokenListsModel =
    new(result, delete)
    result.setup
    result.delegate = delegate
    result.tokensModels = initTable[string, TokensModel]()

  method rowCount(self: TokenListsModel, index: QModelIndex = nil): int =
    return self.delegate.getAllTokenLists().len

  proc countChanged(self: TokenListsModel) {.signal.}
  proc getCount(self: TokenListsModel): int {.slot.} =
    return self.rowCount()
  QtProperty[int] count:
    read = getCount
    notify = countChanged

  method roleNames(self: TokenListsModel): Table[int, string] =
    {
      ModelRole.Id.int:"id",
      ModelRole.Name.int:"name",
      ModelRole.Timestamp.int:"timestamp",
      ModelRole.FetchedTimestamp.int:"fetchedTimestamp",
      ModelRole.Source.int:"source",
      ModelRole.Version.int:"version",
      ModelRole.LogoURI.int:"logoUri",
      ModelRole.Tokens.int:"tokens",
    }.toTable

  proc tokensForListId(self: TokenListsModel, listId: string): var seq[TokenItem] =
    for i in 0 ..< self.delegate.getAllTokenLists().len:
      if self.delegate.getAllTokenLists()[i].id == listId:
        return self.delegate.getAllTokenLists()[i].tokens
    # Row gone: no rows, never another row's tokens.
    self.emptyTokens.setLen(0)
    return self.emptyTokens

  proc getTokensModelDataSource*(self: TokenListsModel, listId: string): TokensModelDataSource =
    return (
      getTokens: proc(): var seq[TokenItem] = self.tokensForListId(listId),
    )

  proc ensureTokensSubmodel*(self: TokenListsModel, listId: string): TokensModel =
    ## One submodel per list id, reused across reads: consumers cache the pointer.
    if not self.tokensModels.hasKey(listId):
      self.tokensModels[listId] = newTokensModel(self.getTokensModelDataSource(listId))
    return self.tokensModels[listId]

  method data(self: TokenListsModel, index: QModelIndex, role: int): QVariant =
    guardModelData(index, self.rowCount(), role, ModelRole)

    var item = self.delegate.getAllTokenLists()[index.row]

    let enumRole = role.ModelRole
    case enumRole:
      of ModelRole.Id:
        return newQVariant(item.id)
      of ModelRole.Name:
        return newQVariant(item.name)
      of ModelRole.Timestamp:
        return newQVariant(item.timestamp)
      of ModelRole.FetchedTimestamp:
        return newQVariant(item.fetchedTimestamp)
      of ModelRole.Source:
        return newQVariant(item.source)
      of ModelRole.Version:
        return newQVariant($item.version)
      of ModelRole.LogoURI:
        return newQVariant(item.logoUri)
      of ModelRole.Tokens:
        return newQVariant(self.ensureTokensSubmodel(item.id))

  proc resetTokensSubmodels(self: TokenListsModel) =
    ## After the full reset: drop removed ids' submodels, reset the survivors.
    var removed: seq[string] = @[]
    for listId, sub in self.tokensModels:
      var found = false
      for item in self.delegate.getAllTokenLists():
        if item.id == listId:
          found = true
          break
      if found:
        sub.modelsUpdated()
      else:
        removed.add(listId)
    for listId in removed:
      # Explicit delete: the self-capturing data-source closure makes the
      # submodel a cycle candidate, so a plain ref drop frees it only at some
      # later cycle collection.
      self.tokensModels[listId].delete
      self.tokensModels.del(listId)

  proc modelsUpdated*(self: TokenListsModel) =
    self.beginResetModel()
    self.endResetModel()
    self.resetTokensSubmodels()

  proc setup(self: TokenListsModel) =
    self.QAbstractListModel.setup

  proc delete(self: TokenListsModel) =
    self.QAbstractListModel.delete