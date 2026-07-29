import app/modules/shared_models/model_utils
import nimqml, tables

import io_interface

type
  ModelRole {.pure.} = enum
    Key = UserRole + 1 # token key
    GroupKey # crossChainId or tokenKey if crossChainId is empty
    CrossChainId
    Name
    Symbol
    ChainId
    Address
    Decimals
    Image
    CustomToken
    CommunityId
    Type

when defined(testing) or defined(QT_MODEL_SPY):
  # Destruction sentinel: QML consumers cache the submodel pointer, so tests
  # assert a handed-out instance is never destroyed while its parent model lives.
  var tokensModelDeleteCount*: int
  # Reset sentinel: content changes must be announced through the cached
  # submodel's own reset; stable refreshes must stay silent.
  var tokensModelResetCount*: int

QtObject:
  type TokensModel* = ref object of QAbstractListModel
    delegate: io_interface.TokensModelDataSource

  proc setup(self: TokensModel)
  proc delete*(self: TokensModel)

  proc newTokensModel*(delegate: io_interface.TokensModelDataSource): TokensModel =
    new(result, delete)
    result.setup
    result.delegate = delegate

  method rowCount(self: TokensModel, index: QModelIndex = nil): int =
    return self.delegate.getTokens().len

  proc countChanged(self: TokensModel) {.signal.}
  proc getCount(self: TokensModel): int {.slot.} =
    return self.rowCount()
  QtProperty[int] count:
    read = getCount
    notify = countChanged

  method roleNames(self: TokensModel): Table[int, string] =
    {
      ModelRole.Key.int:"key",
      ModelRole.GroupKey.int:"groupKey",
      ModelRole.CrossChainId.int:"crossChainId",
      ModelRole.Name.int:"name",
      ModelRole.Symbol.int:"symbol",
      ModelRole.ChainId.int:"chainId",
      ModelRole.Address.int:"address",
      ModelRole.Decimals.int:"decimals",
      ModelRole.Image.int:"image",
      ModelRole.CustomToken.int:"customToken",
      ModelRole.CommunityId.int:"communityId",
      ModelRole.Type.int:"type",
    }.toTable

  method data(self: TokensModel, index: QModelIndex, role: int): QVariant =
    guardModelData(index, self.rowCount(), role, ModelRole)

    let item = self.delegate.getTokens()[index.row]

    let enumRole = role.ModelRole
    case enumRole:
      of ModelRole.Key:
        return newQVariant(item.key)
      of ModelRole.GroupKey:
        return newQVariant(item.groupKey)
      of ModelRole.CrossChainId:
        return newQVariant(item.crossChainId)
      of ModelRole.Name:
        return newQVariant(item.name)
      of ModelRole.Symbol:
        return newQVariant(item.symbol)
      of ModelRole.ChainId:
        return newQVariant(item.chainId)
      of ModelRole.Address:
        return newQVariant(item.address)
      of ModelRole.Decimals:
        return newQVariant(item.decimals)
      of ModelRole.Image:
        return newQVariant(item.logoUri)
      of ModelRole.CustomToken:
        return newQVariant(item.customToken)
      of ModelRole.CommunityId:
        return newQVariant(item.communityData.id)
      of ModelRole.Type:
        return newQVariant(ord(item.`type`))

  proc modelsUpdated*(self: TokensModel) =
    when defined(testing) or defined(QT_MODEL_SPY):
      inc tokensModelResetCount
    self.beginResetModel()
    self.endResetModel()

  proc setup(self: TokensModel) =
    self.QAbstractListModel.setup

  proc delete*(self: TokensModel) =
    when defined(testing) or defined(QT_MODEL_SPY):
      if cast[pointer](self.vptr) != nil:
        inc tokensModelDeleteCount
    self.QAbstractListModel.delete
