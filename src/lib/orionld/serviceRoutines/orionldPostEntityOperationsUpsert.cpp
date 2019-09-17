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
* Author: Gabriel Quaresma
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
#include "orionld/common/orionldEntitiyPayloadCheck.h"                     // payloadCheck


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
    KjNode* next = attrNodeP->next;
    bool    moved = false;

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
bool kjTreeToContextElementAttributes
(
  ConnectionInfo* ciP,
  KjNode* treeP,
  KjNode* createdAtPP,
  KjNode* modifiedAtPP,
  ContextElement* ceP
)
{
  // Iterate over the object, to get all attributes
  while (treeP != NULL)
  {
    KjNode*           attrTypeNodeP = NULL;
    ContextAttribute* caP           = new ContextAttribute();

    if
    (
      treeP != createdAtPP              &&
      treeP != modifiedAtPP             &&
      strcmp(treeP->name, "id") != 0    &&
      strcmp(treeP->name, "type") != 0
    )
    {
      if (treeP->type == KjObject)
      {
        if (orionldAttributeTreat(ciP, treeP, caP, &attrTypeNodeP) == false)
        {
          LM_E(("orionldAttributeTreat failed"));
          delete caP;
          return false;
        }
      }
      //
      // URI Expansion for the attribute name, except if "location", "observationSpace", or "operationSpace"
      //
      if (SCOMPARE9(treeP->name, 'l', 'o', 'c', 'a', 't', 'i', 'o', 'n', 0))
        caP->name = treeP->name;
      else if (SCOMPARE17(treeP->name, 'o', 'b', 's', 'e', 'r', 'v', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
        caP->name = treeP->name;
      else if (SCOMPARE15(treeP->name, 'o', 'p', 'e', 'r', 'a', 't', 'i', 'o', 'n', 'S', 'p', 'a', 'c', 'e', 0))
        caP->name = treeP->name;
      else
      {
        char  longName[256];
        char* details;

        if (orionldUriExpand(orionldState.contextP, treeP->name, longName, sizeof(longName), &details) == false)
        {
          delete caP;
          orionldErrorResponseCreate(OrionldBadRequestData, details, treeP->name, OrionldDetailAttribute);
          return false;
        }

        caP->name = longName;
      }
      // NO URI Expansion for Attribute TYPE
      caP->type = attrTypeNodeP->value.s;
      // Add the attribute to the attr vector
      ceP->contextAttributeVector.push_back(caP);
    }
    treeP = treeP->next;
  }

  return true;
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

  KjNode*  locationP          = NULL;
  KjNode*  observationSpaceP  = NULL;
  KjNode*  operationSpaceP    = NULL;
  KjNode*  createdAtP         = NULL;
  KjNode*  modifiedAtP        = NULL;

  UpdateContextRequest   mongoRequest;
  UpdateContextResponse  mongoResponse;

  mongoRequest.updateActionType = ActionTypeAppendStrict;

  // char* uriParamOption          = orionldState.uriParams.options;
  // LM_TMP(("UpdateParam -> %s", uriParamOption));
  //
  // Debugging - see all IDs
  //
  // if (uriParamOption != NULL && strcmp("update", uriParamOption) == 0)
  // {
  int ix = 0;
  for (KjNode* entityNodeP = orionldState.requestTree->value.firstChildP; entityNodeP != NULL; entityNodeP = entityNodeP->next)
  {
    OBJECT_CHECK(entityNodeP, kjValueType(entityNodeP->type));
    ContextElement*        ceP       = new ContextElement();  // FIXME: Any way I can avoid to allocate ?
    EntityId*              entityIdP;

    if (payloadCheck(ciP, entityNodeP->value.firstChildP, &locationP, &observationSpaceP, &operationSpaceP, &createdAtP, &modifiedAtP, true) == false)
    {
      LM_TMP(("Error at index -> %d", ix));
      return false;
    }

    LM_TMP(("orionldState.payloadIdNode: %s",  orionldState.payloadIdNode->value.s));

    char*   entityId           = orionldState.payloadIdNode->value.s;
    char*   entityType         = orionldState.payloadTypeNode->value.s;
    orionldState.entityId      = entityId;

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
    entityIdP->creDate   = getCurrentTime();
    entityIdP->modDate   = getCurrentTime();

    if (kjTreeToContextElementAttributes(ciP, entityNodeP->value.firstChildP, createdAtP, modifiedAtP, ceP) == false)
    {
      // kjTreeToContextElement fills in error using 'orionldErrorResponseCreate()'
      mongoRequest.release();
      LM_E(("kjTreeToContextElement failed"));
      return false;
    }
    orionldState.payloadIdNode = NULL;
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

    KjNode* successArrayP = kjArray(orionldState.kjsonP, "success");

    for (unsigned int ix = 0; ix < mongoResponse.contextElementResponseVector.vec.size(); ix++)
    {
      KjNode* nodeP = kjString(orionldState.kjsonP, "id", mongoResponse.contextElementResponseVector.vec[ix]->contextElement.entityId.id.c_str());

      //
      // FIXME: Here we assume all items in mongoResponse.contextElementResponseVector are SUCCESSFUL
      //        But, ContextElementResponse has a field called "statusCode" that we could look at ...
      //        The difficult part is ... how to test this?
      //        How provoke a failure?
      //        This service Updates if entity exists and creates if not.
      //        Very difficult to provoke an error ... :(
      //
      kjChildAdd(successArrayP, nodeP);
    }

    kjChildAdd(orionldState.responseTree, successArrayP);
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
  // TODO (Operation with new way)
  // if (mongoCppLegacyEntityOperationsUpsert(orionldState.requestTree) == false)
  // {
  //   LM_E(("mongoCppLegacyEntityOperationsUpsert"));
  //   ciP->httpStatusCode = SccBadRequest;
  //   orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend", OrionldDetailsString);
  //   return false;
  // }
  //
}
