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
#include <string.h>                                            // strchr

extern "C"
{
#include "kjson/KjNode.h"                                      // KjNode
#include "kalloc/kaStrdup.h"                                   // kaStrdup
}

#include "logMsg/logMsg.h"                                      // LM_*
#include "logMsg/traceLevels.h"                                 // Lmt*

#include "orionld/common/orionldState.h"                       // orionldState
#include "orionld/types/OrionldGeoJsonType.h"                  // OrionldGeoJsonType



// ----------------------------------------------------------------------------
//
// pcheckGeoqGeorel -
//
bool pcheckGeoqGeorel(KjNode* georelP, OrionldGeoJsonType geoType, char** detailP)
{
  if (georelP == NULL)
  {
    *detailP = (char*) "georel missing";
    return false;
  }
  else if (georelP->type != KjString)
  {
    *detailP = (char*) "georel must be a string";
    return false;
  }
  else if (georelP->value.s[0] == 0)
  {
    *detailP = (char*) "no value for georel";
    return false;
  }

  char* georel = georelP->value.s;

  LM_TMP(("GEO: georel: %s", georel));

  //
  // Valid values for georel:
  // * near        - Point only
  // * within      - Polygon Only
  // * contains    - Point only
  // * overlaps    - Polygon Only
  // * intersects  - Polygon Only
  // * equals      - Whatever
  // * disjoint    - Polygon Only
  //
  // Any other value and it's an error
  //
  //
  if (strcmp(georel, "near") == 0)
  {
    if (geoType != GeoJsonPoint)
    {
      *detailP = (char*) "The georel value 'near' can only be used with GeoJSON Point";
      return false;
    }
  }
  else if (strcmp(georel, "within") == 0)
  {
  }
  else if (strcmp(georel, "contains") == 0)
  {
  }
  else if (strcmp(georel, "overlaps") == 0)
  {
  }
  else if (strcmp(georel, "intersects") == 0)
  {
  }
  else if (strcmp(georel, "equals") == 0)
  {
  }
  else if (strcmp(georel, "disjoint") == 0)
  {
  }
  else
  {
    LM_W(("Bad Input (invalid georel: '%s')", georel));
  }

  if (geoType == GeoJsonPoint)
  {
    //
    // For a Point, georel can have the following values:
    // - near
    //
    char* extra      = NULL;
    char* grel       = kaStrdup(&orionldState.kalloc, georel);
    char* semicolonP;

    if ((semicolonP = strchr(grel, ';')) != NULL)
    {
      *semicolonP = 0;
      extra = &semicolonP[1];

      if (strcmp(grel, "near") != 0)
      {
        *detailP = (char*) "invalid geo-relation for Point";
        return false;
      }

      //
      // Must be: (max|min)Distance==NUMBER
      //
      char* distance = strstr(extra, "==");

      if (distance == NULL)
      {
        *detailP = (char*) "invalid distance for georel 'near' for Point";
        return false;
      }
      *distance = 0;
      distance = &distance[2];

      if ((strcmp(extra, "maxDistance") != 0) && (strcmp(extra, "minDistance") != 0))
      {
        *detailP = (char*) "invalid distance for georel 'near' for Point";
        return false;
      }

      //
      // 'distance' must be an INTEGER
      //
      while (*distance != 0)
      {
        if ((*distance < '0') || (*distance > '9'))
        {
          *detailP = (char*) "invalid number for distance for georel 'near' for Point";
          return false;
        }
        ++distance;
      }
    }
  }

  return true;
}
