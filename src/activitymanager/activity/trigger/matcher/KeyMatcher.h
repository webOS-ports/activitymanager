// Copyright (c) 2009-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef _KEY_MATCHER_H_
#define _KEY_MATCHER_H_

#include "../Matcher.h"

/*
 * "key" : <keyname>
 */
class KeyMatcher: public Matcher {
public:
    KeyMatcher(const MojString& key);
    virtual ~KeyMatcher();

    virtual bool match(const MojObject& response);

    virtual MojErr toJson(MojObject& rep, unsigned long flags) const;

protected:
    MojString m_key;
};

#endif /* _KEY_MATCHER_H_ */
