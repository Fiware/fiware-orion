/*
*
* Copyright 2020 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Gabriel Quaresma and Ken Zangelin
*/
extern "C"
{
#include "kbase/kMacros.h"                                     // K_FT
#include "kjson/KjNode.h"                                      // KjNode
#include "kjson/kjBuilder.h"                                   // kjString, kjObject, ...
#include "kjson/kjLookup.h"                                    // kjLookup
#include "kjson/kjRender.h"                                    // kjRender
}

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "common/globals.h"                                    // parse8601Time
#include "rest/ConnectionInfo.h"                               // ConnectionInfo
#include "rest/httpHeaderAdd.h"                                // httpHeaderLocationAdd
#include "orionTypes/OrionValueType.h"                         // orion::ValueType
#include "orionTypes/UpdateActionType.h"                       // ActionType
#include "parse/CompoundValueNode.h"                           // CompoundValueNode
#include "ngsi/ContextAttribute.h"                             // ContextAttribute
#include "ngsi10/UpdateContextRequest.h"                       // UpdateContextRequest
#include "ngsi10/UpdateContextResponse.h"                      // UpdateContextResponse
#include "mongoBackend/mongoUpdateContext.h"                   // mongoUpdateContext
#include "rest/uriParamNames.h"                                // URI_PARAM_PAGINATION_OFFSET, URI_PARAM_PAGINATION_LIMIT
#include "mongoBackend/MongoGlobal.h"                          // getMongoConnection()

#include "orionld/rest/orionldServiceInit.h"                   // orionldHostName, orionldHostNameLen
#include "orionld/common/orionldErrorResponse.h"               // orionldErrorResponseCreate
#include "orionld/common/SCOMPARE.h"                           // SCOMPAREx
#include "orionld/common/CHECK.h"                              // CHECK
#include "orionld/common/urlCheck.h"                           // urlCheck
#include "orionld/common/urnCheck.h"                           // urnCheck
#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/context/orionldCoreContext.h"                // orionldDefaultUrl, orionldCoreContext
#include "orionld/context/orionldContextPresent.h"             // orionldContextPresent
#include "orionld/context/orionldContextItemAliasLookup.h"     // orionldContextItemAliasLookup
#include "orionld/context/orionldContextItemExpand.h"          // orionldUriExpand
#include "orionld/kjTree/kjStringValueLookupInArray.h"         // kjStringValueLookupInArray
#include "orionld/kjTree/kjTreeToUpdateContextRequest.h"       // kjTreeToUpdateContextRequest
#include "orionld/serviceRoutines/orionldPostBatchCreate.h"    // Own Interface



// ----------------------------------------------------------------------------
//
// entityErrorPush -
//
// The array "errors" in BatchOperationResult is an array of BatchEntityError.
// BatchEntityError contains a string (the entity id) and an instance of ProblemDetails.
//
// ProblemDetails is described in https://www.etsi.org/deliver/etsi_gs/CIM/001_099/009/01.01.01_60/gs_CIM009v010101p.pdf
// and contains:
//
// * type      (string) A URI reference that identifies the problem type
// * title     (string) A short, human-readable summary of the problem
// * detail    (string) A human-readable explanation specific to this occurrence of the problem
// * status    (number) The HTTP status code
// * instance  (string) A URI reference that identifies the specific occurrence of the problem
//
// Of these five items, only "type" seems to be mandatory.
//
// This implementation will treat "type", "title", and "status" as MANDATORY, and "detail" as OPTIONAL
//
static void entityErrorPush(KjNode* errorsArrayP, const char* entityId, OrionldResponseErrorType type, const char* title, const char* detail, int status)
{
  KjNode* objP            = kjObject(orionldState.kjsonP, NULL);
  KjNode* eIdP            = kjString(orionldState.kjsonP,  "entityId", entityId);
  KjNode* problemDetailsP = kjObject(orionldState.kjsonP,  "error");
  KjNode* typeP           = kjString(orionldState.kjsonP,  "type",     orionldErrorTypeToString(type));
  KjNode* titleP          = kjString(orionldState.kjsonP,  "title",    title);
  KjNode* statusP         = kjInteger(orionldState.kjsonP, "status",   status);

  kjChildAdd(problemDetailsP, typeP);
  kjChildAdd(problemDetailsP, titleP);

  if (detail != NULL)
  {
    KjNode* detailP = kjString(orionldState.kjsonP, "detail", detail);
    kjChildAdd(problemDetailsP, detailP);
  }

  kjChildAdd(problemDetailsP, statusP);

  kjChildAdd(objP, eIdP);
  kjChildAdd(objP, problemDetailsP);

  kjChildAdd(errorsArrayP, objP);
}


// ----------------------------------------------------------------------------
//
// entitySuccessPush -
//
static void entitySuccessPush(KjNode* successArrayP, const char* entityId)
{
  KjNode* eIdP = kjString(orionldState.kjsonP, "id", entityId);

  kjChildAdd(successArrayP, eIdP);
}


// -----------------------------------------------------------------------------
//
// entityIdCheck -
//
static bool entityIdCheck(KjNode* entityIdNodeP, bool duplicatedId, KjNode* errorsArrayP)
{
  // Entity ID is mandatory
  if (entityIdNodeP == NULL)
  {
    LM_W(("Bad Input (UPSERT: mandatory field missing: entity::id)"));
    entityErrorPush(errorsArrayP, "no entity::id", OrionldBadRequestData, "mandatory field missing", "entity::id", 400);
    return false;
  }

  // Entity ID must be a string
  if (entityIdNodeP->type != KjString)
  {
    LM_W(("Bad Input (UPSERT: entity::id not a string)"));
    entityErrorPush(errorsArrayP, "invalid entity::id", OrionldBadRequestData, "field with invalid type", "entity::id", 400);
    return false;
  }

  // Entity ID must be a valid URI
  char* detail;
  if (!urlCheck(entityIdNodeP->value.s, &detail) && !urnCheck(entityIdNodeP->value.s, &detail))
  {
    LM_W(("Bad Input (UPSERT: entity::id is a string but not a valid URI)"));
    entityErrorPush(errorsArrayP, entityIdNodeP->value.s, OrionldBadRequestData, "Not a URI", entityIdNodeP->value.s, 400);
    return false;
  }

  // Entity ID must not be duplicated
  if (duplicatedId == true)
  {
    LM_W(("Bad Input (UPSERT: Duplicated entity::id)"));
    entityErrorPush(errorsArrayP, entityIdNodeP->value.s, OrionldBadRequestData, "Duplicated field", "entity::id", 400);
    return false;
  }

  return true;
}


// -----------------------------------------------------------------------------
//
// entityTypeCheck -
//
static bool entityTypeCheck(KjNode* entityTypeNodeP, bool duplicatedType, char* entityId, bool typeMandatory, KjNode* errorsArrayP)
{
  // Entity TYPE is mandatory?
  if ((typeMandatory == true) && (entityTypeNodeP == NULL))
  {
    LM_W(("Bad Input (UPSERT: mandatory field missing: entity::type)"));
    entityErrorPush(errorsArrayP, entityId, OrionldBadRequestData, "mandatory field missing", "entity::type", 400);
    return false;
  }

  // Entity TYPE must not be duplicated
  if (duplicatedType == true)
  {
    LM_W(("KZ: Bad Input (UPSERT: Duplicated entity::type)"));
    entityErrorPush(errorsArrayP, entityId, OrionldBadRequestData, "Duplicated field", "entity::type", 400);
    return false;
  }

  // Entity TYPE must be a string
  if (entityTypeNodeP->type != KjString)
  {
    LM_W(("Bad Input (UPSERT: entity::type not a string)"));
    entityErrorPush(errorsArrayP, entityId, OrionldBadRequestData, "field with invalid type", "entity::type", 400);
    return false;
  }

  return true;
}


// -----------------------------------------------------------------------------
//
// entityIdAndTypeGet - lookup 'id' and 'type' in a KjTree
//
static bool entityIdAndTypeGet(KjNode* entityNodeP, char** idP, char** typeP, KjNode* errorsArrayP)
{
  KjNode*  idNodeP        = NULL;
  KjNode*  typeNodeP      = NULL;
  bool     idDuplicated   = false;
  bool     typeDuplicated = false;

  *idP   = NULL;
  *typeP = NULL;

  for (KjNode* itemP = entityNodeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (SCOMPARE3(itemP->name, 'i', 'd', 0))
    {
      if (idNodeP != NULL)
        idDuplicated = true;
      else
        idNodeP = itemP;
    }
    else if (SCOMPARE5(itemP->name, 't', 'y', 'p', 'e', 0))
    {
      if (typeNodeP != NULL)
        typeDuplicated = true;
      else
        typeNodeP = itemP;
    }
  }

  if (entityIdCheck(idNodeP, idDuplicated, errorsArrayP) == false)
  {
    LM_E(("UPSERT: entityIdCheck flagged error"));
    return false;
  }

  *idP = idNodeP->value.s;

  if (typeNodeP != NULL)
  {
    if (entityTypeCheck(typeNodeP, typeDuplicated, idNodeP->value.s, false, errorsArrayP) == false)
    {
      LM_E(("CREATE: entityTypeCheck flagged error"));
      return false;
    }

    *typeP = typeNodeP->value.s;
  }

  LM_E(("CREATE: OK"));

  return true;
}


// ----------------------------------------------------------------------------
//
// entityIdPush - add ID to array
//
static void entityIdPush(KjNode* entityIdsArrayP, const char* entityId)
{
  KjNode* idNodeP = kjString(orionldState.kjsonP, NULL, entityId);

  kjChildAdd(entityIdsArrayP, idNodeP);
}


// -----------------------------------------------------------------------------
//
// entityLookupById - lookup an entity in an array of entities, by its entity-id
//
static KjNode* entityLookupById(KjNode* entityArray, char* entityId)
{
  for (KjNode* entityP = entityArray->value.firstChildP; entityP != NULL; entityP = entityP->next)
  {
    KjNode* idNodeP = kjLookup(entityP, "id");

    if ((idNodeP != NULL) && (strcmp(idNodeP->value.s, entityId) == 0))  // If NULL, something is really wrong!!!
      return entityP;
  }

  return NULL;
}


// -----------------------------------------------------------------------------
//
// entityIdGet -
//
static void entityIdGet(KjNode* dbEntityP, char** idP)
{
  for (KjNode* nodeP = dbEntityP->value.firstChildP; nodeP != NULL; nodeP = nodeP->next)
  {
    if (SCOMPARE3(nodeP->name, 'i', 'd', 0))
      *idP = nodeP->value.s;
  }
}


// ----------------------------------------------------------------------------
//
// typeCheckForNonExistingEntities -
//
bool typeCheckForNonExistingEntities(KjNode* incomingTree, KjNode* idTypeAndCreDateFromDb, KjNode* errorsArrayP)
{
  KjNode* inNodeP = incomingTree->value.firstChildP;
  KjNode* next;

  while (inNodeP != NULL)
  {
    //
    // entities that weren't found in the DB MUST contain entity::type
    //
    KjNode* inEntityIdNodeP = kjLookup(inNodeP, "id");

    if (inEntityIdNodeP == NULL)  // Entity ID is mandatory
    {
      LM_E(("KZ: Invalid Entity: Mandatory field entity::id is missing"));
      entityErrorPush(errorsArrayP, "No ID", OrionldBadRequestData, "Invalid Entity", "Mandatory field entity::id is missing", 400);
      next = inNodeP->next;
      kjChildRemove(incomingTree, inNodeP);
      inNodeP = next;
      continue;
    }

    KjNode* dbEntityId = NULL;

    // Lookup the entity::id in what came from the database - if anything at all came
    if (idTypeAndCreDateFromDb != NULL)
      dbEntityId = entityLookupById(idTypeAndCreDateFromDb, inEntityIdNodeP->value.s);

    if (dbEntityId == NULL)  // This Entity is to be created - "type" is mandatory!
    {
      KjNode* inEntityTypeNodeP = kjLookup(inNodeP, "type");

      if (inEntityTypeNodeP == NULL)
      {
        LM_E(("KZ: Invalid Entity: Mandatory field entity::type is missing"));
        entityErrorPush(errorsArrayP, inEntityIdNodeP->value.s, OrionldBadRequestData, "Invalid Entity", "Mandatory field entity::type is missing", 400);

        next = inNodeP->next;
        kjChildRemove(incomingTree, inNodeP);
        inNodeP = next;
        continue;
      }
    }

    inNodeP = inNodeP->next;
  }

  return true;
}


// ----------------------------------------------------------------------------
//
// orionldPostBatchCreate -
//
// POST /ngsi-ld/v1/entityOperations/create
//
// From the spec:
//   This operation allows creating a batch of NGSI-LD Entities, creating each of them if they does not exist.
//
bool orionldPostBatchCreate(ConnectionInfo* ciP)
{
  //
  // Prerequisites for the payload in orionldState.requestTree:
  // * must be an array
  // * cannot be empty
  // * all entities must contain a entity::id (one level down)
  // * no entity can contain an entity::type (one level down)
  //
  ARRAY_CHECK(orionldState.requestTree, "toplevel");
  EMPTY_ARRAY_CHECK(orionldState.requestTree, "toplevel");
  

  KjNode*               incomingTree   = orionldState.requestTree;
  KjNode*               idArray        = kjArray(orionldState.kjsonP, NULL);
  KjNode*               successArrayP  = kjArray(orionldState.kjsonP, "success");
  KjNode*               errorsArrayP   = kjArray(orionldState.kjsonP, "errors");
  KjNode*               entityP;
  KjNode*               next;

  //
  // 01. Create idArray as an array of entity IDs, extracted from orionldState.requestTree
  //
  entityP = incomingTree->value.firstChildP;
  while (entityP)
  {
    next = entityP->next;

    char* entityId;
    char* entityType;

    // entityIdAndTypeGet calls entityIdCheck/entityTypeCheck that adds the entity in errorsArrayP if needed
    if (entityIdAndTypeGet(entityP, &entityId, &entityType, errorsArrayP) == true)
      entityIdPush(idArray, entityId);
    else
      kjChildRemove(incomingTree, entityP);

    entityP = next;
  }

  //
  // 02. Query database extracting three fields: { id, type and creDate } for each of the entities
  //     whose Entity::Id is part of the array "idArray".
  //     The result is "idTypeAndCredateFromDb" - an array of "tiny" entities with { id, type and creDate }
  //
  KjNode* idTypeAndCreDateFromDb = dbEntityListLookupWithIdTypeCreDate(idArray);

  if (idTypeAndCreDateFromDb != NULL)
  {
    for (KjNode* dbEntityP = idTypeAndCreDateFromDb->value.firstChildP; dbEntityP != NULL; dbEntityP = dbEntityP->next)
    {
      char*    idInDb        = NULL;
      KjNode*  entityP;

      // Get entity id, type and creDate from the DB
      entityIdGet(dbEntityP, &idInDb);
      entityErrorPush(errorsArrayP, idInDb, OrionldBadRequestData, "entity already exists", NULL, 400);
      
      entityP = entityLookupById(incomingTree, idInDb);
      
      kjChildRemove(incomingTree, entityP);
    }
  }

  typeCheckForNonExistingEntities(incomingTree, idTypeAndCreDateFromDb, errorsArrayP);

  UpdateContextRequest  mongoRequest;

  mongoRequest.updateActionType = ActionTypeAppendStrict;

  kjTreeToUpdateContextRequest(ciP, &mongoRequest, incomingTree, errorsArrayP);

  UpdateContextResponse mongoResponse;

  ciP->httpStatusCode = mongoUpdateContext(&mongoRequest,
                                           &mongoResponse,
                                           orionldState.tenant,
                                           ciP->servicePathV,
                                           ciP->uriParam,
                                           ciP->httpHeaders.xauthToken,
                                           ciP->httpHeaders.correlator,
                                           ciP->httpHeaders.ngsiv2AttrsFormat,
                                           ciP->apiVersion,
                                           NGSIV2_NO_FLAVOUR);

  if (ciP->httpStatusCode == SccOk)
  {
    orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);

    for (unsigned int ix = 0; ix < mongoResponse.contextElementResponseVector.vec.size(); ix++)
    {
      const char* entityId = mongoResponse.contextElementResponseVector.vec[ix]->contextElement.entityId.id.c_str();

      if (mongoResponse.contextElementResponseVector.vec[ix]->statusCode.code == SccOk)
        entitySuccessPush(successArrayP, entityId);
      else
        entityErrorPush(errorsArrayP,
                        entityId,
                        OrionldBadRequestData,
                        "",
                        mongoResponse.contextElementResponseVector.vec[ix]->statusCode.reasonPhrase.c_str(),
                        400);
    }

    for (unsigned int ix = 0; ix < mongoRequest.contextElementVector.vec.size(); ix++)
    {
      const char* entityId = mongoRequest.contextElementVector.vec[ix]->entityId.id.c_str();

      if (kjStringValueLookupInArray(successArrayP, entityId) == NULL)
        entitySuccessPush(successArrayP, entityId);
    }

    //
    // Add the success/error arrays to the response-tree
    //
    kjChildAdd(orionldState.responseTree, successArrayP);
    kjChildAdd(orionldState.responseTree, errorsArrayP);

    ciP->httpStatusCode = SccOk;
  }

  mongoRequest.release();
  mongoResponse.release();

  if (ciP->httpStatusCode != SccOk)
  {
    LM_E(("mongoUpdateContext flagged an error"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Database Error");
    ciP->httpStatusCode = SccReceiverInternalError;
    return false;
  }

  return true;   
}