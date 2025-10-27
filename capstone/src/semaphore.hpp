#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <tbrs/types.hpp>
#include <volk/volk.h>
#include <limits>
#include <atomic>

class Fence {
	public:
		void wait() const;
		u64 value() const;
	
		Fence() = default;

	private:
		Fence(VkDevice m_device, VkSemaphore semaphore, u64 value);
	
		VkDevice m_device = {};
		VkSemaphore m_semaphore = {};
		u64 m_value = 0;
	
		friend class Semaphore;
};

class Semaphore {
	public:
		u64 submittedValue() const;
		u64 signaledValue() const;
		u64 advance(u64 value = std::numeric_limits<u64>::max());
	
		void wait(u64 value = std::numeric_limits<u64>::max()) const;
		void signal(u64 value = std::numeric_limits<u64>::max());
	
		Fence fence() const;
		VkSemaphore semaphore() const;

		Semaphore() = default;
		Semaphore(VkDevice device);
		~Semaphore();
	
		Semaphore(const Semaphore&) = delete;
		Semaphore& operator=(const Semaphore&) = delete;
		Semaphore(Semaphore&& src) {
			memcpy(this, &src, sizeof(Semaphore));
			memset(&src, 0, sizeof(Semaphore));
		}
		Semaphore& operator=(Semaphore&& src) {
			this->~Semaphore();
			new (this) Semaphore(std::move(src)); return *this;
		};
	
	private:
	
		VkDevice m_device = {};
		VkSemaphore m_semaphore = {};
		std::atomic<u64> m_value = 0;
};

#endif