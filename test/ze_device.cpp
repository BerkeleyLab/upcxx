#include <upcxx/upcxx.hpp>
#include <iostream>
#include "util.hpp"

#if !UPCXX_KIND_ZE
#error This test requires ze_device support
#endif

using namespace upcxx;

#include <level_zero/ze_api.h>

device_allocator<ze_device>
my_make_allocator(size_t seg_size, 
                  ze_device_handle_t zeDevice, 
                  ze_context_handle_t zeContext) {
  // share the oneAPI Driver Context with UPC++
  ze_device::set_driver_context(zeContext, zeDevice);

  // open the GPU and create device segment:
  return make_gpu_allocator<ze_device>(seg_size,
    ze_device::device_handle_to_device_id(zeDevice));
}

int main() {
  upcxx::init();
  print_test_header();
 
  if (!upcxx::rank_me()) 
    say() << "UPCXX_KIND_ZE=" << UPCXX_KIND_ZE;

  size_t seg_size = 2<<20;
  // Create a GPU device segment, using automatic device selection:
  auto gpu_alloc = make_gpu_allocator<ze_device>(seg_size);
  // Query the ID of the opened device:
  int id = gpu_alloc.device_id();
  // Retrieve oneAPI handles corresponding to this device:
  ze_device_handle_t zeDevice = ze_device::device_id_to_device_handle(id);
  ze_driver_handle_t zeDriver = ze_device::device_id_to_driver_handle(id);
  ze_context_handle_t zeContext = ze_device::get_driver_context(zeDevice);
 
  // validate that returned oneAPI handles look sane:
  UPCXX_ASSERT_ALWAYS(zeDevice);
  UPCXX_ASSERT_ALWAYS(zeDriver);
  UPCXX_ASSERT_ALWAYS(zeContext);

  // ... and function in Level Zero calls:
  UPCXX_ASSERT_ALWAYS( ZE_RESULT_SUCCESS ==
    zeContextGetStatus(zeContext) ); 

  ze_driver_properties_t dri_prop = { ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES };
  UPCXX_ASSERT_ALWAYS( ZE_RESULT_SUCCESS ==
    zeDriverGetProperties(zeDriver, &dri_prop));

  ze_device_properties_t deviceProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
  UPCXX_ASSERT_ALWAYS( ZE_RESULT_SUCCESS ==
    zeDeviceGetProperties(zeDevice, &deviceProperties) ); 

  // Use them to construct a new device segment:
  auto gpu_alloc2 = my_make_allocator(seg_size, zeDevice, zeContext);
  UPCXX_ASSERT_ALWAYS(gpu_alloc.is_active());

  // cleanup
  gpu_alloc.destroy();
  gpu_alloc2.destroy();

  print_test_success(); 
  upcxx::finalize();
  return 0;
}
