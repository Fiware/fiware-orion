/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
* Author: Ken Zangelin
*/
#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "rest/ConnectionInfo.h"                                 // ConnectionInfo
#include "rest/HttpStatusCode.h"                                 // SccNotFound
#include "ngsi/ContextElement.h"                                 // ContextElement

#include "mongoBackend/mongoUpdateContext.h"                     // mongoUpdateContext
#include "mongoBackend/mongoQueryContext.h"                      // mongoQueryContext

#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/SCOMPARE.h"                             // SCOMPAREx
#include "orionld/common/CHECK.h"                                // *CHECK*
#include "orionld/common/urlCheck.h"                             // urlCheck
#include "orionld/common/urnCheck.h"                             // urnCheck
#include "orionld/kjTree/kjTreeFromQueryContextResponse.h"       // kjTreeFromQueryContextResponse
#include "orionld/kjTree/kjTreeToContextAttribute.h"             // kjTreeToContextAttribute
#include "orionld/context/orionldContextItemExpand.h"            // orionldUriExpand
#include "orionld/mongoBackend/mongoAttributeExists.h"           // mongoAttributeExists
#include "orionld/mongoBackend/mongoEntityExists.h"              // mongoEntityExists
#include "orionld/db/dbConfiguration.h"                          // dbRegistrationLookup
#include "orionld/serviceRoutines/orionldPatchAttribute.h"       // Own Interface



// -----------------------------------------------------------------------------
//
// kjTreeAttributeValidate -
//
static bool kjTreeAttributeValidate(KjNode* attrTree, KjValueType* attrValueTypeP, KjValue* attrValueP)
{
  // validate the payload content informed by user e get the attribute value

  for (KjNode* userPayloadNodeP = attrTree->value.firstChildP; userPayloadNodeP != NULL; userPayloadNodeP = userPayloadNodeP->next)
  {
    if (SCOMPARE9(userPayloadNodeP->name, '@', 'c', 'o', 'n', 't', 'e', 'x', 't', 0))
    {
      // Do Nothing
    }
    else if (SCOMPARE6(userPayloadNodeP->name, 'v', 'a', 'l', 'u', 'e', 0))  // the updated attr is a property or geoproperty
    {
      // Get the attribute value and the attribute value type
      *attrValueP     = userPayloadNodeP->value;
      *attrValueTypeP = userPayloadNodeP->type;
    }
    else if (SCOMPARE7(userPayloadNodeP->name, 'o', 'b', 'j', 'e', 'c', 't', 0))  // the updated attr is a relationship
    {
      // Get the attribute object and the attrbibue value type
      *attrValueP     = userPayloadNodeP->value;
      *attrValueTypeP = userPayloadNodeP->type;
    }
    else
    {
      // Invalid key in payload
      orionldErrorResponseCreate(OrionldBadRequestData, "Invalid key in the payload:", userPayloadNodeP->name);
      return false;
    }
  }

  return true;
}


// -----------------------------------------------------------------------------
//
// orionldForwardPatchEntity
//
static bool orionldForwardPatchEntity(const char* entityId, const char* attrName, KjNode* payloadData, KjNode* regArray)
{
  return false;
}



// ----------------------------------------------------------------------------
//
// orionldPatchAttribute -
//
bool orionldPatchAttribute(ConnectionInfo* ciP)
{
  char*          entityId      = orionldState.wildcard[0];
  char*          attrName      = orionldState.wildcard[1];
  KjValueType    attrValueType = KjNone;
  KjValue        attrValue;
  KjNode*        regArray;

  LM_TMP(("FWD: In orionldPatchAttribute"));

  PAYLOAD_EMPTY_CHECK();
  PAYLOAD_IS_OBJECT_CHECK();
  PAYLOAD_EMPTY_OBJECT_CHECK();

  if (kjTreeAttributeValidate(orionldState.requestTree, &attrValueType, &attrValue) == false)
  {
    ciP->httpStatusCode = SccBadRequest;
    return false;
  }

  //
  // Expand the attribute name, if not a special attribute - special attributes aren't expanded
  //
  if ((strcmp(attrName, "location") != 0) && (strcmp(attrName, "observationSpace") != 0) && (strcmp(attrName, "operationSpace") != 0))
    attrName = orionldContextItemExpand(orionldState.contextP, orionldState.wildcard[1], NULL, true, NULL);

  //
  // Lookup any matching Registrations
  //
  regArray = dbRegistrationLookup(entityId, attrName);

  if (regArray != NULL)
  {
    LM_TMP(("FWD: Registration found for entity-attribute combination '%s' - '%s'", entityId, attrName));
    return orionldForwardPatchEntity(entityId, attrName, orionldState.requestTree, regArray);
  }
  LM_TMP(("FWD: No registration found for entity-attribute combination '%s' - '%s'", entityId, attrName));

  // Make sure the entity to be patched exists
  if (mongoEntityExists(entityId, orionldState.tenant) == false)
  {
    ciP->httpStatusCode = SccNotFound;
    orionldErrorResponseCreate(OrionldBadRequestData, "Entity does not exist", orionldState.wildcard[0]);
    return false;
  }

  // Make sure the attribute to be patched exists
  if (mongoAttributeExists(entityId, attrName, orionldState.tenant) == false)
  {
    ciP->httpStatusCode = SccNotFound;
    orionldErrorResponseCreate(OrionldBadRequestData, "Attribute does not exist", orionldState.wildcard[1]);
    return false;
  }

  bool                  keyValues = false;
  QueryContextRequest   request;
  QueryContextResponse  response;
  EntityId              updatedEntityId(orionldState.wildcard[0], "", "false", false);
  char*                 details;

  request.entityIdVector.push_back(&updatedEntityId);

  //
  // Make sure the ID (orionldState.wildcard[0]) is a valid URI
  //
  if ((urlCheck(orionldState.wildcard[0], &details) == false) && (urnCheck(orionldState.wildcard[0], &details) == false))
  {
    LM_W(("Bad Input (Invalid Entity ID - Not a URL nor a URN)"));
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", "Not a URL nor a URN");
    return false;
  }


  // retrieve the updated entity from database
  ciP->httpStatusCode = mongoQueryContext(&request,
                                          &response,
                                          orionldState.tenant,
                                          ciP->servicePathV,
                                          ciP->uriParam,
                                          ciP->uriParamOptions,
                                          NULL,
                                          ciP->apiVersion);

  if (response.errorCode.code == SccBadRequest)
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Bad Request", NULL);
    return false;
  }

  KjNode* myEntity     = kjTreeFromQueryContextResponse(ciP, true, NULL , keyValues, &response);
  KjNode* updatedAttrP = NULL;

  if (myEntity == NULL)
    ciP->httpStatusCode = SccContextElementNotFound;
  else
  {
    // update the attribute value based on content informed by user
    for (KjNode* kNodeP = myEntity->value.firstChildP; kNodeP != NULL; kNodeP = kNodeP->next)
    {
      if (strcmp(kNodeP->name, orionldState.wildcard[1]) == 0)
      {
        updatedAttrP = kNodeP;

        for (KjNode* attrP = updatedAttrP->value.firstChildP; attrP != NULL; attrP = attrP->next)
        {
          if (SCOMPARE6(attrP->name, 'v',  'a', 'l', 'u', 'e', 0))  // update the property or geoproperty value
          {
            attrP->value = attrValue;
            attrP->type  = attrValueType;
            break;
          }
          else if (SCOMPARE7(attrP->name, 'o',  'b', 'j', 'e', 'c', 't', 0))  // update the relationship value
          {
            attrP->value = attrValue;
            attrP->type  = attrValueType;
            break;
          }
        }

        break;
      }
    }
  }

  // Store the new attribute value in the database
  KjNode*                attrTypeNodeP = NULL;
  UpdateContextRequest   mongoRequest;
  UpdateContextResponse  mongoResponse;
  EntityId*              entityIdP;
  ContextElement*        ceP        = new ContextElement();  // FIXME: Any way I can avoid to allocate ?
  ContextAttribute*      caP        = new ContextAttribute();
  char*                  detail;


  mongoRequest.contextElementVector.push_back(ceP);

  entityIdP                     = &mongoRequest.contextElementVector[0]->entityId;
  entityIdP->id                 = entityId;
  mongoRequest.updateActionType = ActionTypeAppendStrict;

  if (kjTreeToContextAttribute(ciP, updatedAttrP, caP, &attrTypeNodeP, &detail) == false)
  {
    mongoRequest.release();
    delete caP;
    return false;
  }

  if (attrTypeNodeP != NULL)
    ceP->contextAttributeVector.push_back(caP);
  else
    delete caP;

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
  if (ciP->httpStatusCode == SccOk)
    ciP->httpStatusCode = SccNoContent;
  else
  {
    LM_E(("mongoUpdateContext: HTTP Status Code: %d", ciP->httpStatusCode));
    orionldErrorResponseCreate(OrionldBadRequestData, "Internal Error", "Error from Mongo-DB backend");
    return false;
  }

  return true;
}
