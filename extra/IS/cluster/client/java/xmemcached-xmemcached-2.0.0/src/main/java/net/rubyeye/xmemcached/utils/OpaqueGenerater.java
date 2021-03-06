/**
 *Copyright [2009-2010] [dennis zhuang(killme2008@gmail.com)]
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License. 
 *You may obtain a copy of the License at 
 *             http://www.apache.org/licenses/LICENSE-2.0 
 *Unless required by applicable law or agreed to in writing, 
 *software distributed under the License is distributed on an "AS IS" BASIS, 
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, 
 *either express or implied. See the License for the specific language governing permissions and limitations under the License
 */
/**
 *Copyright [2009-2010] [dennis zhuang(killme2008@gmail.com)]
 *Licensed under the Apache License, Version 2.0 (the "License");
 *you may not use this file except in compliance with the License.
 *You may obtain a copy of the License at
 *             http://www.apache.org/licenses/LICENSE-2.0
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 *either express or implied. See the License for the specific language governing permissions and limitations under the License
 */
package net.rubyeye.xmemcached.utils;

import java.util.concurrent.atomic.AtomicInteger;

/**
 * Opaque generator for memcached binary xxxq(getq,addq etc.) commands
 * 
 * @author dennis
 * 
 */
public final class OpaqueGenerater {
	private OpaqueGenerater() {

	}

	private AtomicInteger counter = new AtomicInteger(0);

	static final class SingletonHolder {
		static final OpaqueGenerater opaqueGenerater = new OpaqueGenerater();
	}

	public static OpaqueGenerater getInstance() {
		return SingletonHolder.opaqueGenerater;
	}

	// just for test.
	public void setValue(int v) {
		this.counter.set(v);
	}

	public int getNextValue() {
		int val = counter.incrementAndGet();
		if (val < 0) {
			while (val < 0 && !counter.compareAndSet(val, 0)) {
				val = counter.get();
			}
			return counter.incrementAndGet();
		} else {
			return val;
		}
	}

}
