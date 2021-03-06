/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _MISC_H_
#define _MISC_H_

// Api for recovery write key
int RecoveryWriteKey(const char *keyOptarg);

// Api for recovery secure check
int RecoverySecureCheck(const char *zipPath);

// Api for recovery adb sideload
void adb_listeners(RecoveryUI* ui, int argc, char **argv);

#endif  /* _MISC_H_ */
