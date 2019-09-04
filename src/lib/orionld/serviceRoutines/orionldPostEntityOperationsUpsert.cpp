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
// orionldPostEntityOperationsUpsert -
//
bool orionldPostEntityOperationsUpsert(ConnectionInfo* ciP)
{
  ARRAY_CHECK(orionldState.requestTree, "toplevel");

  char*    details;
  KjNode*  locationP          = NULL;
  KjNode*  observationSpaceP  = NULL;
  KjNode*  operationSpaceP    = NULL;
  KjNode*  createdAtP         = NULL;
  KjNode*  modifiedAtP        = NULL;

  UpdateContextRequest   mongoRequest;
  UpdateContextResponse  mongoResponse;
  ContextElement*        ceP       = new ContextElement();  // FIXME: Any way I can avoid to allocate ?
  EntityId*              entityIdP;

  if (orionldState.requestTree->type != KjArray)
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "invalid payload", "must be a JSON array", OrionldDetailsString);
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Debugging - see all IDs
  //
  int ix = 0;
  for (KjNode* entityNodeP = orionldState.requestTree->value.firstChildP; entityNodeP != NULL; entityNodeP = entityNodeP->next)
  {
    if (payloadCheck(ciP, entityNodeP->value.firstChildP, &locationP, &observationSpaceP, &operationSpaceP, &createdAtP, &modifiedAtP, true) == false)
    {
      LM_TMP(("Error at index -> %d", ix));
      return false;
    }

    LM_TMP(("orionldState.payloadIdNode: %s",  orionldState.payloadIdNode->value.s));

    char*    entityId           = orionldState.payloadIdNode->value.s;
    char*    entityType         = orionldState.payloadTypeNode->value.s;

    orionldState.entityId = entityId;

    mongoRequest.contextElementVector.push_back(ceP);
    entityIdP = &mongoRequest.contextElementVector[ix]->entityId;
    mongoRequest.updateActionType = ActionTypeAppend;

    entityIdP->id        = entityId;
    entityIdP->type      = (orionldState.payloadTypeNode != NULL)? orionldState.payloadTypeNode->value.s : NULL;
    entityIdP->isPattern = "false";
    entityIdP->creDate   = getCurrentTime();
    entityIdP->modDate   = getCurrentTime();

    //
    // Entity TYPE
    //
    entityIdP->isTypePattern = false;

    LM_T(LmtUriExpansion, ("Looking up uri expansion for the entity type '%s'", entityType));
    LM_T(LmtUriExpansion, ("------------- uriExpansion for Entity Type starts here ------------------------------"));

    char*  expandedName;
    char*  expandedType;

    // FIXME: Call orionldUriExpand() - this here is a "copy" of what orionldUriExpand does
    extern int uriExpansion(OrionldContext* contextP, const char* name, char** expandedNameP, char** expandedTypeP, char** detailsPP);
    int    expansions = uriExpansion(orionldState.contextP, entityType, &expandedName, &expandedType, &details);
    LM_T(LmtUriExpansion, ("URI Expansion for type '%s': '%s'", entityType, expandedName));
    LM_T(LmtUriExpansion, ("Got %d expansions", expansions));

    if (expansions == 0)
    {
      // Expansion found in Core Context - perform no expansion
    }
    else if (expansions == 1)
    {
      // Take the long name, just ... NOT expandedType but expandedName. All good
      entityIdP->type = expandedName;
    }
    else if (expansions == -2)
    {
      // No expansion found in Core Context, and in no other contexts either - use default URL
      LM_T(LmtUriExpansion, ("EXPANSION: use default URL for entity type '%s'", entityType));
      entityIdP->type = orionldDefaultUrl;
      entityIdP->type += entityType;
    }
    else if (expansions == -1)
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid context item for 'entity type'", details, OrionldDetailsString);
      mongoRequest.release();
      return false;
    }
    else  // expansions == 2 ... may be an incorrect context
    {
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value of context item 'entity type'", orionldState.contextP->url, OrionldDetailsString);
      mongoRequest.release();
      return false;
    }

    // Treat the entire payload
    for (KjNode* kNodeP = entityNodeP->value.firstChildP; kNodeP != NULL; kNodeP = kNodeP->next)
    {
      LM_TMP(("treating entity node '%s'", kNodeP->name));

      if (kNodeP == createdAtP)
      {
        // FIXME: Make sure the value is a valid timestamp?

        // Ignore the 'createdAt' - See issue #43 (orionld)
  #if 0
        // 2. Save it for future use (when creating the entity)
        if ((orionldState.overriddenCreationDate = parse8601Time(createdAtP->value.s)) == -1)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for 'createdAt' attribute", createdAtP->value.s, OrionldDetailsString);
          mongoRequest.release();
          return false;
        }
  #endif
      }
      else if (kNodeP == modifiedAtP)
      {
        // FIXME: Make sure the value is a valid timestamp?

        // Ignore the 'createdAt' - See issue #43 (orionld)
  #if 0
        // 2. Save it for future use (when creating the entity)
        if ((orionldState.overriddenModificationDate = parse8601Time(modifiedAtP->value.s)) == -1)
        {
          orionldErrorResponseCreate(OrionldBadRequestData, "Invalid value for 'modifiedAt' attribute", modifiedAtP->value.s, OrionldDetailsString);
          mongoRequest.release();
          return false;
        }
  #endif
      }
      else  // Must be an attribute
      {
        LM_T(LmtPayloadCheck, ("Not createdAt/modifiedAt: '%s' - treating as ordinary attribute", kNodeP->name));

        ContextAttribute* caP            = new ContextAttribute();
        KjNode*           attrTypeNodeP  = NULL;

        if (strcmp(kNodeP->name, "@context") != 0 && strcmp(kNodeP->name, "id") != 0 && strcmp(kNodeP->name, "type") != 0)
        {
          if (orionldAttributeTreat(ciP, kNodeP, caP, &attrTypeNodeP) == false)
          {
            LM_E(("orionldAttributeTreat failed"));
            delete caP;
            mongoRequest.release();
            return false;
          }
          //
          // NO URI Expansion for Attribute TYPE
          //
          caP->type = attrTypeNodeP->value.s;
        }

        //
        // URI Expansion for Attribute NAME - except if its name is one of the following three:
        // 1. location
        // 2. observationSpace
        // 3. operationSpace
        //
        if ((strcmp(kNodeP->name, "location") == 0) || (strcmp(kNodeP->name, "observationSpace") == 0) || (strcmp(kNodeP->name, "operationSpace") == 0))
        {
          caP->name = kNodeP->name;
        }
        else
        {
          char longName[256];

          if (orionldUriExpand(orionldState.contextP, kNodeP->name, longName, sizeof(longName), &details) == false)
          {
            LM_E(("orionldUriExpand failed"));
            delete caP;
            mongoRequest.release();
            orionldErrorResponseCreate(OrionldBadRequestData, details, kNodeP->name, OrionldDetailsAttribute);
            return false;
          }

          caP->name = longName;
        }

        ceP->contextAttributeVector.push_back(caP);
      }
    }

    orionldState.payloadIdNode = NULL;
    orionldState.payloadTypeNode = NULL;
    ++ix;
  }

  LM_TMP(("treating entity node '%s'", entityIdP->id.c_str()));
  LM_TMP(("mongoRequest->contextElementVector.size(): %d", mongoRequest.contextElementVector.size()));

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

  mongoRequest.release();
  mongoResponse.release();

  if (ciP->httpStatusCode != SccOk)
  {
    LM_E(("mongoUpdateContext: HTTP Status Code: %d", ciP->httpStatusCode));
    orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend", OrionldDetailsString);
    return false;
  }


  // if (mongoCppLegacyEntityOperationsUpsert(orionldState.requestTree) == false)
  // {
  //   LM_E(("mongoCppLegacyEntityOperationsUpsert"));
  //   ciP->httpStatusCode = SccBadRequest;
  //   orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend", OrionldDetailsString);
  //   return false;
  // }
    
  ciP->httpStatusCode = SccOk;
  return true;
}
