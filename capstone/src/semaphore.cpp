#include "semaphore.hpp"
#include <tbrs/vk_util.hpp>

namespace {
	void atomic_max(std::atomic<u64>& obj, u64 val) {
		for(
			u64 old = obj.load(std::memory_order_relaxed);
			old < val && !obj.compare_exchange_weak(old, val, std::memory_order_relaxed, std::memory_order_relaxed);
		);
	}
}

void Fence::wait() const {
	if(m_device && m_semaphore) {
		vkWaitSemaphores(m_device, ptr(VkSemaphoreWaitInfo{
			.semaphoreCount = 1,
			.pSemaphores = &m_semaphore,
			.pValues = &m_value,
		}), std::numeric_limits<u64>::max());
	}
}

u64 Fence::value() const {
	return m_value;
}

Fence::Fence(VkDevice device, VkSemaphore semaphore, u64 value) : m_device(device), m_semaphore(semaphore), m_value(value) {}

u64 Semaphore::submittedValue() const {
	return m_value;
}

u64 Semaphore::signaledValue() const {
	u64 ret;
	vkGetSemaphoreCounterValue(m_device, m_semaphore, &ret);

	return ret;
}

u64 Semaphore::advance(u64 value) {
	if(value == std::numeric_limits<u64>::max()) {
		m_value++;
	}
	else {
		atomic_max(m_value, value);
	}
	return m_value;
}

void Semaphore::wait(u64 value) const {
	if(value == std::numeric_limits<u64>::max()) {
		value = m_value.load(std::memory_order_relaxed);
	}

	vkWaitSemaphores(m_device, ptr(VkSemaphoreWaitInfo{
		.semaphoreCount = 1,
		.pSemaphores = &m_semaphore,
		.pValues = &value,
	}), std::numeric_limits<u64>::max());
}

void Semaphore::signal(u64 value) {
	if(value != std::numeric_limits<u64>::max()) {
		atomic_max(m_value, value);
	}

	vkSignalSemaphore(m_device, ptr(VkSemaphoreSignalInfo{
		.semaphore = m_semaphore,
		.value = m_value.load(),
	}));
}

Fence Semaphore::fence() const {
	return Fence(m_device, m_semaphore, m_value);
}

VkSemaphore Semaphore::semaphore() const {
	return m_semaphore;
}

Semaphore::~Semaphore() {
	// moved from or default constructed produces a null device member, which is invalid
	if(m_device) {
		vkDestroySemaphore(m_device, m_semaphore, nullptr);
	}
}

Semaphore::Semaphore(VkDevice device) : m_device(device) {
	vkCreateSemaphore(device, ptr(VkSemaphoreCreateInfo{
		.pNext = ptr(VkSemaphoreTypeCreateInfo {
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = 0
		})
	}), nullptr, &m_semaphore);
}

Semaphore::Semaphore(Semaphore&& src) {
	memcpy(this, &src, sizeof(Semaphore));
	memset(&src, 0, sizeof(Semaphore));
}
Semaphore& Semaphore::operator=(Semaphore&& src) {
	this->~Semaphore();
	new (this) Semaphore(std::move(src)); return *this;
};