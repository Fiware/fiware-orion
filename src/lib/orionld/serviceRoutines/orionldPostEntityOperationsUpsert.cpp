/*
*
* Copyright 2019 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Gabriel Quaresma ans Ken Zangelin
*/
#include "logMsg/logMsg.h"                                                 // LM_*
#include "logMsg/traceLevels.h"                                            // Lmt*

extern "C"
{
#include "kjson/KjNode.h"                                                  // KjNode
#include "kjson/kjBuilder.h"                                               // kjString, kjObject, ...
#include "kjson/kjRender.h"                                                // kjRender
}

#include "common/globals.h"                                                // parse8601Time
#include "rest/ConnectionInfo.h"                                           // ConnectionInfo
#include "rest/httpHeaderAdd.h"                                            // httpHeaderLocationAdd
#include "orionTypes/OrionValueType.h"                                     // orion::ValueType
#include "orionTypes/UpdateActionType.h"                                   // ActionType
#include "parse/CompoundValueNode.h"                                       // CompoundValueNode
#include "ngsi/ContextAttribute.h"                                         // ContextAttribute
#include "ngsi10/UpdateContextRequest.h"                                   // UpdateContextRequest
#include "ngsi10/UpdateContextResponse.h"                                  // UpdateContextResponse
#include "mongoBackend/mongoEntityExists.h"                                // mongoEntityExists
#include "mongoBackend/mongoUpdateContext.h"                               // mongoUpdateContext
#include "rest/uriParamNames.h"                                            // URI_PARAM_PAGINATION_OFFSET, URI_PARAM_PAGINATION_LIMIT

#include "orionld/rest/orionldServiceInit.h"                               // orionldHostName, orionldHostNameLen
#include "orionld/common/orionldErrorResponse.h"                           // orionldErrorResponseCreate
#include "orionld/common/SCOMPARE.h"                                       // SCOMPAREx
#include "orionld/common/CHECK.h"                                          // CHECK
#include "orionld/common/urlCheck.h"                                       // urlCheck
#include "orionld/common/urnCheck.h"                                       // urnCheck
#include "orionld/common/orionldState.h"                                   // orionldState
#include "orionld/common/orionldAttributeTreat.h"                          // orionldAttributeTreat
#include "orionld/context/orionldCoreContext.h"                            // orionldDefaultUrl, orionldCoreContext
#include "orionld/context/orionldContextAdd.h"                             // Add a context to the context list
#include "orionld/context/orionldContextLookup.h"                          // orionldContextLookup
#include "orionld/context/orionldContextItemLookup.h"                      // orionldContextItemLookup
#include "orionld/context/orionldContextList.h"                            // orionldContextHead, orionldContextTail
#include "orionld/context/orionldContextListInsert.h"                      // orionldContextListInsert
#include "orionld/context/orionldContextPresent.h"                         // orionldContextPresent
#include "orionld/context/orionldUserContextKeyValuesCheck.h"              // orionldUserContextKeyValuesCheck
#include "orionld/context/orionldUriExpand.h"                              // orionldUriExpand
#include "orionld/serviceRoutines/orionldPostEntityOperationsUpsert.h"     // Own Interface
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityOperationsUpsert.h"   // mongoCppLegacyEntityOperationsUpsert


// ----------------------------------------------------------------------------
//
// orionldPartialUpdateResponseCreateBatch -
//
void orionldPartialUpdateResponseCreateBatch(ConnectionInfo* ciP)
{
  //
  // Rob the incoming Request Tree - performance to be won!
  //
  orionldState.responseTree = orionldState.requestTree;
  orionldState.requestTree  = NULL;

  //
  // For all attrs in orionldState.responseTree, remove those that are found in orionldState.errorAttributeArrayP.
  // Remember, the format of orionldState.errorAttributeArrayP is:
  //
  //   |attrName|attrName|attrName|...
  //

  KjNode* attrNodeP = orionldState.responseTree->value.firstChildP;

  while (attrNodeP != NULL)
  {
    char*   match;
    KjNode* next   = attrNodeP->next;
    bool    moved  = false;

    if ((match = strstr(orionldState.errorAttributeArrayP, attrNodeP->name)) != NULL)
    {
      if ((match[-1] == '|') && (match[strlen(attrNodeP->name)] == '|'))
      {
        kjChildRemove(orionldState.responseTree, attrNodeP);
        attrNodeP = next;
        moved = true;
      }
    }

    if (moved == false)
      attrNodeP = attrNodeP->next;
  }
}



// -----------------------------------------------------------------------------
//
// kjTreeToContextElementBatch -
//
// NOTE: "id" and "type" of the entity must be removed from the tree before this function is called
//
bool kjTreeToContextElementAttributes
(
  ConnectionInfo*  ciP,
  KjNode*          entityNodeP,
  KjNode*          createdAtP,
  KjNode*          modifiedAtP,
  ContextElement*  ceP,
  char**           detailP
)
{
  // Iterate over the items of the entity
  for (KjNode* itemP = entityNodeP->value.firstChildP; itemP != NULL; itemP = itemP->next)
  {
    if (itemP == createdAtP)
      continue;
    if (itemP == modifiedAtP)
      continue;

    // No key-values in batch ops
    if (itemP->type != KjObject)
    {
      *detailP = (char*) "attribute must be a JSON object";
      return false;
    }

    KjNode*            attrTypeNodeP  = NULL;
    ContextAttribute*  caP            = new ContextAttribute();

    if (orionldAttributeTreat(ciP, itemP, caP, &attrTypeNodeP, detailP) == false)
    {
      LM_E(("orionldAttributeTreat failed"));
      delete caP;
      return false;
    }

    //
    // URI Expansion for the attribute name, except if "location", "observationSpace", or "operationSpace"
    //
    if (SCOMPARE9(itemP->name, 'l', 'o', 'c', 'a', 't', 'i', 'o', 'n', 0))
      caP->name = itemP->name;
    else if (SCOMPARE17(itemP->name, 'o', 'b', 's', 'e', 'r', 'v', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
      caP->name = itemP->name;
    else if (SCOMPARE15(itemP->name, 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
      caP->name = itemP->name;
    else
    {
      char  longName[256];
      char* details;

      if (orionldUriExpand(orionldState.contextP, itemP->name, longName, sizeof(longName), &details) == false)
      {
        delete caP;
        orionldErrorResponseCreate(OrionldBadRequestData, details, itemP->name, OrionldDetailAttribute);
        return false;
      }

      caP->name = longName;
    }

    caP->type = attrTypeNodeP->value.s;
    ceP->contextAttributeVector.push_back(caP);
  }

  return true;
}



// ----------------------------------------------------------------------------
//
// entitySuccessPush -
//
static void entitySuccessPush(KjNode** successsArrayPP, const char* entityId)
{
  KjNode* eIdP = kjString(orionldState.kjsonP, "id", entityId);

  if (*successsArrayPP == NULL)
    *successsArrayPP = kjArray(orionldState.kjsonP, "success");

  kjChildAdd(*successsArrayPP, eIdP);
}



// ----------------------------------------------------------------------------
//
// entityErrorPush -
//
static void entityErrorPush(KjNode** errorsArrayPP, const char* entityId, const char* reason)
{
  KjNode* objP    = kjObject(orionldState.kjsonP, NULL);
  KjNode* eIdP    = kjString(orionldState.kjsonP, "entityId", entityId);
  KjNode* reasonP = kjString(orionldState.kjsonP, "error",    reason);

  kjChildAdd(objP, eIdP);
  kjChildAdd(objP, reasonP);

  if (*errorsArrayPP == NULL)
    *errorsArrayPP = kjArray(orionldState.kjsonP, "errors");

  kjChildAdd(*errorsArrayPP, objP);
}



// ----------------------------------------------------------------------------
//
// orionldPostEntityOperationsUpsert -
//
bool orionldPostEntityOperationsUpsert(ConnectionInfo* ciP)
{
  ciP->httpStatusCode = SccOk;
  ARRAY_CHECK(orionldState.requestTree,       "toplevel");
  EMPTY_ARRAY_CHECK(orionldState.requestTree, "toplevel");

  KjNode*  createdAtP         = NULL;
  KjNode*  modifiedAtP        = NULL;

  UpdateContextRequest   mongoRequest;
  UpdateContextResponse  mongoResponse;

  KjNode* successArrayP = NULL;
  KjNode* errorsArrayP  = NULL;

  mongoRequest.updateActionType = ActionTypeAppendStrict;

  // char* uriParamOption          = orionldState.uriParams.options;
  // LM_TMP(("UpdateParam -> %s", uriParamOption));
  //
  // if (uriParamOption != NULL && strcmp("update", uriParamOption) == 0)
  // {
  int ix = 0;
  for (KjNode* entityNodeP = orionldState.requestTree->value.firstChildP; entityNodeP != NULL; entityNodeP = entityNodeP->next)
  {
    OBJECT_CHECK(entityNodeP, kjValueType(entityNodeP->type));

    //
    // First, extract Entity::id and Entity::type
    //
    // As we will remove items from the tree, we need to save the 'next-pointer' a priori
    // If not, after removing an item, its next pointer point to NULL and the for-loop (if used) is ended
    //

    KjNode*   next;
    KjNode*   itemP           = entityNodeP->value.firstChildP;
    KjNode*   entityIdNodeP   = NULL;
    KjNode*   entityTypeNodeP = NULL;
    while (itemP != NULL)
    {
      LM_TMP(("Batch: got item '%s'", itemP->name));
      if (SCOMPARE3(itemP->name, 'i', 'd', 0))
      {
        LM_TMP(("Batch: got Entity::ID"));
        if (entityIdNodeP != NULL)
          entityErrorPush(&errorsArrayP, entityIdNodeP->value.s, "Entity ID must be a unique field");
        // Make sure Entity ID is a string and a valid URL
        entityIdNodeP = itemP;
        next = itemP->next;
        kjChildRemove(entityNodeP, entityIdNodeP);
      }
      else if (SCOMPARE5(itemP->name, 't', 'y', 'p', 'e', 0))
      {
        LM_TMP(("Batch: got Entity::TYPE"));
        if (entityTypeNodeP != NULL)
          entityErrorPush(&errorsArrayP, entityIdNodeP->value.s, "Entity TYPE must be a unique field");

        // Make sure  Entity TYPE is a string
        entityTypeNodeP = itemP;
        next = itemP->next;
        kjChildRemove(entityNodeP, entityTypeNodeP);
      }
      else
        next = itemP->next;

      itemP = next;
    }

    //
    // Entity ID is mandatory
    //
    char* detail;

    if (entityIdNodeP == NULL)
    {
      entityErrorPush(&errorsArrayP, "NO Entity-ID", "Entity ID is mandatory");
      continue;
    }

    if (entityIdNodeP->type != KjString)
    {
      entityErrorPush(&errorsArrayP, "Invalid Entity-ID", "Entity ID must be a JSON string");
      continue;
    }

    if ((urlCheck(entityIdNodeP->value.s, &detail) == false) && (urnCheck(entityIdNodeP->value.s, &detail) == false))
    {
      entityErrorPush(&errorsArrayP, entityIdNodeP->value.s, "Entity ID must be a valid URI");
      continue;
    }


    //
    // Entity TYPE is mandatory
    //
    if (entityTypeNodeP == NULL)
    {
      entityErrorPush(&errorsArrayP, entityIdNodeP->value.s, "Entity TYPE missing - mandatory");
      continue;
    }

    if (entityTypeNodeP->type != KjString)
    {
      entityErrorPush(&errorsArrayP, entityIdNodeP->value.s, "Entity TYPE must be a JSON string");
      continue;
    }


    //
    // Both Entity::id and Entity::type are OK
    //
    char*   entityId           = entityIdNodeP->value.s;
    char*   entityType         = entityTypeNodeP->value.s;

    ContextElement*  ceP = new ContextElement();  // FIXME: Any way I can avoid to allocate ?
    EntityId*        entityIdP;

    mongoRequest.contextElementVector.push_back(ceP);
    entityIdP                     = &mongoRequest.contextElementVector[ix]->entityId;
    mongoRequest.updateActionType = ActionTypeAppendStrict;

    char  typeExpanded[256];
    char* details;

    entityIdP->id = entityId;

    if (orionldUriExpand(orionldState.contextP, entityType, typeExpanded, sizeof(typeExpanded), &details) == false)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Error during URI expansion of entity type", details, OrionldDetailString);
      return false;
    }

    entityIdP->type      = typeExpanded;
    entityIdP->isPattern = "false";

#if 0
    entityIdP->creDate   = getCurrentTime();  // FIXME: Only if newly created. I think mongoBackend takes care of this - so, outdeffed
    entityIdP->modDate   = getCurrentTime();
#endif

    if (kjTreeToContextElementAttributes(ciP, entityNodeP, createdAtP, modifiedAtP, ceP, &detail) == false)
    {
      LM_W(("kjTreeToContextElementAttributes flags error '%s' for entity '%s'", detail, entityId));
      entityErrorPush(&errorsArrayP, entityId, detail);
      continue;
    }

    orionldState.payloadIdNode   = NULL;
    orionldState.payloadTypeNode = NULL;
    ++ix;
  }
  // }

  //
  // Call mongoBackend
  //
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

  //
  // Now check orionldState.errorAttributeArray to see whether any attribute failed to be updated
  //
  // bool partialUpdate = (orionldState.errorAttributeArrayP[0] == 0)? false : true;
  // bool retValue      = true;

  LM_E(("mongoUpdateContext: HTTP Status Code: %d", ciP->httpStatusCode));

  if (ciP->httpStatusCode == SccOk)
  {
    LM_TMP(("Items in mongoResponse.contextElementResponseVector: %d", mongoResponse.contextElementResponseVector.size()));
    orionldState.responseTree = kjObject(orionldState.kjsonP, NULL);

    for (unsigned int ix = 0; ix < mongoResponse.contextElementResponseVector.vec.size(); ix++)
    {
      const char* entityId = mongoResponse.contextElementResponseVector.vec[ix]->contextElement.entityId.id.c_str();

      //
      // FIXME: Here we assume all items in mongoResponse.contextElementResponseVector are SUCCESSFUL
      //        But, ContextElementResponse has a field called "statusCode" that we could look at ...
      //
      //        Something like this:
      //
      //          if (mongoResponse.contextElementResponseVector.vec[ix]->statusCode.code == SccOk) { SUCCESS} else { ERROR }
      //
      //        The difficult part is ... how to test this?
      //        How provoke a failure?
      //        This service Updates if entity exists and creates if not.
      //        Very difficult to provoke an error ... :(
      //
      //        It can be tested with gmock, but I'm not an expert ...
      //
      entitySuccessPush(&successArrayP, entityId);
    }

    if (successArrayP != NULL)
      kjChildAdd(orionldState.responseTree, successArrayP);

    if (errorsArrayP != NULL)
      kjChildAdd(orionldState.responseTree, errorsArrayP);

    ciP->httpStatusCode = SccOk;
  }
  else
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Bad Request", "ERROR", OrionldDetailString);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  mongoRequest.release();
  mongoResponse.release();

  return true;

  //
  // TO-DO (Operation with new way)
  // if (mongoCppLegacyEntityOperationsUpsert(orionldState.requestTree) == false)
  // {
  //   LM_E(("mongoCppLegacyEntityOperationsUpsert"));
  //   ciP->httpStatusCode = SccBadRequest;
  //   orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend", OrionldDetailsString);
  //   return false;
  // }
  //
}
