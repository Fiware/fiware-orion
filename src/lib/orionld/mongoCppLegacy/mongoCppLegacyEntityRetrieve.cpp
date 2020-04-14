/*
*
* Copyright 2019 FIWARE Foundation e.V.
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
#include "mongo/client/dbclient.h"                                  // MongoDB C++ Client Legacy Driver

extern "C"
{
#include "kbase/kMacros.h"                                          // K_FT
#include "kjson/KjNode.h"                                           // KjNode
#include "kjson/kjLookup.h"                                         // kjLookup
#include "kjson/kjBuilder.h"                                        // kjObject, ...
#include "kjson/kjRender.h"                                         // kjRender
}

#include "logMsg/logMsg.h"                                          // LM_*
#include "logMsg/traceLevels.h"                                     // Lmt*

#include "mongoBackend/MongoGlobal.h"                               // getMongoConnection, releaseMongoConnection, ...
#include "orionld/common/eqForDot.h"                                // eqForDot
#include "orionld/db/dbCollectionPathGet.h"                         // dbCollectionPathGet
#include "orionld/db/dbConfiguration.h"                             // dbDataToKjTree
#include "orionld/mongoCppLegacy/mongoCppLegacyDbStringFieldGet.h"  // mongoCppLegacyDbStringFieldGet
#include "orionld/mongoCppLegacy/mongoCppLegacyDbObjectFieldGet.h"  // mongoCppLegacyDbObjectFieldGet
#include "orionld/mongoCppLegacy/mongoCppLegacyEntityRetrieve.h"    // Own interface



// ----------------------------------------------------------------------------
//
// mongoCppLegacyEntityRetrieve -
//
// PARAMERTERS
//   entityId        ID of the entity to be retrieved
//   attrs           array of attribute names, terminated by a NULL pointer
//   attrMandatory   If true - the entity is found only if any of the attributes in 'attrs'
//                   is present in the entity
//
KjNode* mongoCppLegacyEntityRetrieve(const char* entityId, char** attrs, bool attrMandatory)
{
  char    collectionPath[256];
  KjNode* attrTree  = NULL;
  KjNode* dbTree    = NULL;

  dbCollectionPathGet(collectionPath, sizeof(collectionPath), "entities");


  //
  // Populate 'queryBuilder' - only Entity ID for this operation
  //
  mongo::BSONObjBuilder  queryBuilder;
  queryBuilder.append("_id.id", entityId);


  // 1. Get _id - make a kj tree
  // 2. Get attrs - make a kj tree
  // 3. Change "value" to "object" for all attributes that are "Relationship"
  // 4. Get @dataset - make a tree
  // 5. Move every dataset attribute values to its corresponding attribute in attrs
  // 6. Merge _id and attrs into one single tree
  //


  // semTake()
  mongo::DBClientBase*                  connectionP = getMongoConnection();
  std::auto_ptr<mongo::DBClientCursor>  cursorP;
  mongo::Query                          query(queryBuilder.obj());
  mongo::BSONObj                        retFieldsObj;
  mongo::BSONObjBuilder                 retFieldsBuilder;

  retFieldsBuilder.append("attrs", 1);
  retFieldsBuilder.append("@datasets", 1);
  retFieldsObj = retFieldsBuilder.obj();

  //
  // Querying mongo and retrieving the results
  //
  LM_TMP(("NQ: Making the query"));
  cursorP = connectionP->query(collectionPath, query, 0, 0, &retFieldsObj);
  LM_TMP(("NQ: Retrieving the results"));

  while (cursorP->more())
  {
    LM_TMP(("NQ: Calling nextSafe"));
    mongo::BSONObj  bsonObj = cursorP->nextSafe();
    char*           title;
    char*           details;

    LM_TMP(("NQ: Calling dbDataToKjTree"));
    dbTree = dbDataToKjTree(&bsonObj, &title, &details);
    if (dbTree == NULL)
      LM_E(("%s: %s", title, details));
    break;
  }

  releaseMongoConnection(connectionP);
  // semGive()

  if (dbTree == NULL)
  {
    // Entity nt found
    return NULL;
  }

  // <DEBUG>
  char buf[1024];
  kjRender(orionldState.kjsonP, dbTree, buf, sizeof(buf));
  LM_TMP(("NQ: DB-Tree: %s", buf));
  // </DEBUG>
  
  int      includedAttributes = 0;
  KjNode*  dbAttrsP           = kjLookup(dbTree, "attrs");      // Must be there
  KjNode*  dbDataSetsP        = kjLookup(dbTree, "@datasets");  // May not be there

  if (dbAttrsP == NULL)
  {
    LM_E(("Internal Error (field 'attrs' not found for entity '%s')", entityId));
    return NULL;
  }

  //
  // Attributes may be found both in dbAttrsP abd in dbDataSetsP
  //
  if (attrs == NULL)     // Include all attributes in the response
  {
    attrTree = dbAttrsP;

    //
    // Add "@datasets" to the attributes
    //
    
  }
  else
  {
    attrTree = kjObject(orionldState.kjsonP, NULL);

    int     ix = 0;
    KjNode* attrP;
    KjNode* datasetP;

    while (attrs[ix] != NULL)
    {
      attrP    = kjLookup(dbAttrsP, attrs[ix]);
      datasetP = (dbDataSetsP == NULL)? NULL : kjLookup(dbDataSetsP, attrs[ix]);

      if ((datasetP != NULL) && (attrP != NULL))
      {
        kjChildRemove(dbDataSetsP, datasetP);
        kjChildAdd(attrTree, datasetP);

        kjChildRemove(dbAttrsP, attrP);
        kjChildAdd(datasetP, attrP);
        ++includedAttributes;
      }
      else if (attrP != NULL)
      {
        kjChildRemove(dbAttrsP, attrP);
        kjChildAdd(attrTree, attrP);
        ++includedAttributes;
      }
      else if (datasetP != NULL)
      {
        kjChildRemove(dbDataSetsP, datasetP);
        kjChildAdd(attrTree, datasetP);
      }

      ++ix;
    }
      
    //
    // Change "value" to "object" for all attributes that are "Relationship".
    // Note that the "object" field of a Relationship is stored in the database under the field "value".
    // That fact is fixed here, by renaming the "value" to "object" for attr with type == Relationship.
    // This depends on the database model and thus should be fixed in the database layer.
    //
    for (KjNode* attrP = attrTree->value.firstChildP; attrP != NULL; attrP = attrP->next)
    {
      if (attrP->type == KjObject)
      {
        KjNode* typeP = kjLookup(attrP, "type");

        if (typeP == NULL)
        {
          LM_E(("Database Error (field 'type' not found for attribute '%s' of entity '%s')", attrP->name, entityId));
          return NULL;
        }

        if (strcmp(typeP->value.s, "Relationship") == 0)
        {
          KjNode* objectP = kjLookup(attrP, "value");

          if (objectP == NULL)
          {
            LM_E(("Database Error (field 'value' not found for attribute '%s' of entity '%s')", attrP->name, entityId));
            return NULL;
          }

          objectP->name = (char*) "object";
        }
      }
      else  // KjArray
      {
        LM_TMP(("ToDo: change 'value' for 'object' in all instances of attribute '%s'", attrP->name));
      }
    }
  }

  if ((includedAttributes == 0) && (attrMandatory == true))
  {
    // 404 not found ...
    // The Entity exists, but it doesn't have ANY of the attributes in attrsP
    return NULL;
  }

  KjNode* idP = kjLookup(dbTree, "_id");

  if (idP == NULL)
  {
    LM_E(("Internal Error (field '_id' not found for entity '%s')", entityId));
    return NULL;
  }

  // Merge idP and attrTree
  if (attrTree != NULL)
  {
    idP->lastChild->next = attrTree->value.firstChildP;
    idP->lastChild       = attrTree->lastChild;
  }
      
  return idP;
}
